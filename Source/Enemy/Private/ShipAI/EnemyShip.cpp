// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/EnemyShip.h"
#include "Cannon.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"
#include "Components/WidgetComponent.h"
#include "Components/BaseHealthComponent.h"
#include "BaseAttributeSet.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BuoyancyComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UI/HealthBarWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/Abilities/GA_EnemyShipDeployObstacle.h"
#include "ShipAI/Abilities/GA_EnemyShipTimeStop.h"
#include "UObject/UnrealType.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowEnemyShipAIDebug(
		TEXT("p.ShowEnemyShipAIDebug"),
		0,
		TEXT("Draw Enemy Ship AI ranges, state, abilities, and cooldowns. 0=off, 1=on."),
		ECVF_Cheat);

	TAutoConsoleVariable<float> CVarEnemyShipAIDebugHeight(
		TEXT("p.EnemyShipAIDebugHeight"),
		200.0f,
		TEXT("Vertical offset in cm for p.ShowEnemyShipAIDebug range lines."),
		ECVF_Cheat);
}

AEnemyShip::AEnemyShip()
{
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
	NavigationComponent = CreateDefaultSubobject<UEnemyShipNavigationComponent>(TEXT("EnemyShipNavigationComponent"));
	PatternRuntimeComponent = CreateDefaultSubobject<UEnemyShipPatternRuntimeComponent>(TEXT("EnemyShipPatternRuntimeComponent"));
	HealthBarWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidgetComponent"));
	HealthBarWidgetComponent->SetupAttachment(RootComponent);
	HealthBarWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidgetComponent->SetDrawAtDesiredSize(false);
	HealthBarWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FClassFinder<UHealthBarWidget> HealthBarWidgetFinder(TEXT("/Game/Blueprints/02_UI/UI_HUD/WBP_HealthBarWidget"));
	if (HealthBarWidgetFinder.Succeeded())
	{
		HealthBarWidgetClass = HealthBarWidgetFinder.Class;
		HealthBarWidgetComponent->SetWidgetClass(HealthBarWidgetClass);
	}

	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));
	LegacyAbilityBootstrapClasses = {
		UGA_EnemyShipCharge::StaticClass(),
		UGA_EnemyShipLaunchTorpedo::StaticClass(),
		UGA_EnemyShipDeployObstacle::StaticClass(),
		UGA_EnemyShipTimeStop::StaticClass()
	};

	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetCollisionProfileName(TEXT("EnemyShip"));
	}
	if (ShipDamageMesh)
	{
		ShipDamageMesh->SetCollisionProfileName(TEXT("EnemyShipDamage"));
	}
}

void AEnemyShip::BeginPlay()
{
	Super::BeginPlay();
	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));

	// HealthComponent를 Ship의 ASC에 바인딩 (BaseEnemy의 패턴과 동일)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &AEnemyShip::OnDeathStarted);
			HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AEnemyShip::OnHealthChanged);
			HealthComponent->OnMaxHealthChanged.AddUniqueDynamic(this, &AEnemyShip::OnMaxHealthChanged);
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
	}

	InitializeHealthBarWidget();

	if (HasAuthority())
	{
		MigrateLegacyNavigationAuthoring();
		if (EnemyShipArchetype)
		{
			EnemyShipArchetype->ApplyToShip(this);
		}
		else if (NavigationComponent)
		{
			// LEGACY: Remove this fallback after every Enemy Ship BP has an Archetype.
			FEnemyShipNavigationProfile LegacyProfile = NavigationComponent->GetNavigationProfile();
			LegacyProfile.IdealDistance = FMath::Max(1.0f, IdealDistance);
			LegacyProfile.ReturnArrivalDistance = FMath::Max(0.0f, NavigationHomeArrivalDistance);
			LegacyProfile.MaxActiveCannons = FMath::Max(1, MaxActiveCannons);
			NavigationComponent->SetNavigationProfile(LegacyProfile);
			GrantEnemyShipAbilityClasses(LegacyAbilityBootstrapClasses);
		}

		if (NavigationComponent)
		{
			NavigationComponent->SetHomeActor(NavigationHomeActor);
		}
	}

	// 캐싱된 대포 목록 탐색
	// Drop에 관한 정보 초기화
	InitializeEnemyDropData();

	// 0.5초마다 타겟과 가장 가까운 N개의 대포를 선정해 목록을 갱신하는 타이머 작동
	if (HasAuthority() && !EnemyShipArchetype && bLegacyAutomaticCannonFireWithoutArchetype)
	{
		GetWorldTimerManager().SetTimer(ActiveCannonsTimerHandle, this, &AEnemyShip::UpdateActiveCannons, 0.5f, true);
	}

	// 군집 서브시스템에 등록
	if (HasAuthority())
	{
		if (NavigationComponent)
		{
			NavigationComponent->ClearAllOverrides();
			NavigationComponent->SetNavigationEnabled(false);
		}
		if (UShipSwarmSubsystem* SwarmSubsystem = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->RegisterShip(this);
		}
	}
}

void AEnemyShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			for (const FGameplayAbilitySpecHandle Handle : GrantedEnemyShipAbilityHandles)
			{
				ASC->ClearAbility(Handle);
			}
		}
		GrantedEnemyShipAbilityHandles.Reset();

		if (UShipSwarmSubsystem* SwarmSubsystem = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->UnregisterShip(this);
		}
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &AEnemyShip::OnDeathStarted);
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &AEnemyShip::OnHealthChanged);
		HealthComponent->OnMaxHealthChanged.RemoveDynamic(this, &AEnemyShip::OnMaxHealthChanged);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AEnemyShip::MigrateLegacyNavigationAuthoring()
{
	// LEGACY: One-time bridge for old BP-authored ReturnPointActor and
	// ReturnArrivalOffset variables. Delete after content migration M11.
	if (!NavigationHomeActor)
	{
		if (const FObjectPropertyBase* ReturnPointProperty = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("ReturnPointActor")))
		{
			NavigationHomeActor = Cast<AActor>(ReturnPointProperty->GetObjectPropertyValue_InContainer(this));
		}
	}

	if (const FNumericProperty* ArrivalOffsetProperty = FindFProperty<FNumericProperty>(GetClass(), TEXT("ReturnArrivalOffset")))
	{
		const void* ValueAddress = ArrivalOffsetProperty->ContainerPtrToValuePtr<void>(this);
		const float LegacyDistance = static_cast<float>(ArrivalOffsetProperty->GetFloatingPointPropertyValue(ValueAddress));
		if (LegacyDistance > 0.0f)
		{
			NavigationHomeArrivalDistance = LegacyDistance;
		}
	}
}

bool AEnemyShip::GrantEnemyShipAbilities(const UEnemyShipAbilitySet* AbilitySet)
{
	if (!HasAuthority() || !AbilitySet)
	{
		return false;
	}
	return GrantEnemyShipAbilityClasses(AbilitySet->Abilities);
}

bool AEnemyShip::GrantEnemyShipAbilityClasses(
	const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses)
{
	if (!HasAuthority())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	for (const FGameplayAbilitySpecHandle Handle : GrantedEnemyShipAbilityHandles)
	{
		ASC->ClearAbility(Handle);
	}
	GrantedEnemyShipAbilityHandles.Reset();

	TSet<UClass*> SeenClasses;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		if (AbilityClass && !SeenClasses.Contains(AbilityClass.Get()))
		{
			SeenClasses.Add(AbilityClass.Get());
			GrantedEnemyShipAbilityHandles.Add(ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1)));
		}
	}
	return GrantedEnemyShipAbilityHandles.Num() == SeenClasses.Num();
}

bool AEnemyShip::ConfigureEnemyShipPattern(UEnemyShipPatternData* Pattern)
{
	if (!HasAuthority() || !Pattern || !NavigationComponent || !PatternRuntimeComponent)
	{
		return false;
	}

	TArray<UEnemyShipSkillModuleData*> RawCoreModules;
	for (UEnemyShipSkillModuleData* Module : CoreSkillModules)
	{
		if (IsValid(Module))
		{
			RawCoreModules.AddUnique(Module);
		}
	}
	PatternRuntimeComponent->SetCoreSkillModules(RawCoreModules);
	PatternRuntimeComponent->SetPattern(Pattern);
	NavigationComponent->SetNavigationProfile(Pattern->NavigationProfile);

	TArray<TSubclassOf<UGameplayAbility>> AbilityClasses;
	TSet<const UEnemyShipSkillModuleData*> SeenModules;
	auto AppendModuleAbilities = [&AbilityClasses, &SeenModules](const UEnemyShipSkillModuleData* Module)
	{
		if (!IsValid(Module) || SeenModules.Contains(Module) || !Module->AbilitySet)
		{
			return;
		}
		SeenModules.Add(Module);
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : Module->AbilitySet->Abilities)
		{
			AbilityClasses.AddUnique(AbilityClass);
		}
	};
	for (const UEnemyShipSkillModuleData* Module : CoreSkillModules)
	{
		AppendModuleAbilities(Module);
	}
	for (const UEnemyShipSkillModuleData* Module : Pattern->SkillModules)
	{
		AppendModuleAbilities(Module);
	}
	return GrantEnemyShipAbilityClasses(AbilityClasses);
}

void AEnemyShip::SetCoreSkillModules(const TArray<UEnemyShipSkillModuleData*>& InCoreModules)
{
	CoreSkillModules.Reset();
	for (UEnemyShipSkillModuleData* Module : InCoreModules)
	{
		if (IsValid(Module))
		{
			CoreSkillModules.AddUnique(Module);
		}
	}
}

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CVarShowEnemyShipAIDebug.GetValueOnGameThread() > 0)
	{
		DrawEnemyShipAIDebug();
	}

	if (HasAuthority() && !bDeathHandled && !EnemyShipArchetype
		&& bLegacyAutomaticCannonFireWithoutArchetype)
	{
		TickAIAimingAndFiring(DeltaTime);
	}
}

void AEnemyShip::DrawEnemyShipAIDebug() const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !NavigationComponent)
	{
		return;
	}

	const FEnemyShipNavigationProfile& Profile = NavigationComponent->GetNavigationProfile();
	const float HeightOffset = FMath::Max(0.0f, CVarEnemyShipAIDebugHeight.GetValueOnGameThread());
	const FVector Center = GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
	constexpr int32 Segments = 96;
	constexpr float Thickness = 2.5f;
	constexpr uint8 DepthPriority = SDPG_Foreground;
	const FVector PlaneAxisX = FVector::ForwardVector;
	const FVector PlaneAxisY = FVector::RightVector;

	auto DrawRange = [World, PlaneAxisX, PlaneAxisY](
		const FVector& RangeCenter,
		float Radius,
		const FColor& Color,
		const TCHAR* Label)
	{
		if (Radius <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		DrawDebugCircle(
			World, RangeCenter, Radius, Segments, Color, false, 0.0f,
			DepthPriority, Thickness, PlaneAxisX, PlaneAxisY, false);
		DrawDebugString(
			World,
			RangeCenter + FVector(Radius, 0.0f, 15.0f),
			FString::Printf(TEXT("%s %.0fcm"), Label, Radius),
			nullptr,
			Color,
			0.0f,
			false,
			0.8f);
	};

	DrawRange(Center, Profile.DangerCloseDistance, FColor::Red, TEXT("DangerClose"));
	DrawRange(Center, Profile.IdealDistance, FColor::Green, TEXT("Ideal"));
	DrawRange(
		Center,
		Profile.IdealDistance + Profile.OrbitTolerance,
		FColor::Yellow,
		TEXT("OrbitMax"));
	DrawRange(Center, Profile.DetectionDistance, FColor::Cyan, TEXT("Detection"));

	if (const AActor* HomeActor = NavigationComponent->GetHomeActor())
	{
		const FVector HomeCenter = HomeActor->GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
		DrawRange(HomeCenter, Profile.ReturnArrivalDistance, FColor::Magenta, TEXT("ReturnArrival"));
		DrawDebugLine(World, Center, HomeCenter, FColor::Magenta, false, 0.0f, DepthPriority, 1.5f);
	}

	const AShip* TargetShip = NavigationComponent->GetTargetShip();
	const float TargetDistance = TargetShip
		? FVector::Dist2D(GetActorLocation(), TargetShip->GetActorLocation())
		: -1.0f;
	if (TargetShip)
	{
		const FVector TargetPoint = TargetShip->GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
		DrawDebugLine(World, Center, TargetPoint, FColor::White, false, 0.0f, DepthPriority, 2.0f);
	}

	const FString StateName = StaticEnum<ENavalCombatState>()->GetNameStringByValue(
		static_cast<int64>(NavigationComponent->GetCurrentState()));
	const UEnemyShipPatternData* Pattern = PatternRuntimeComponent
		? PatternRuntimeComponent->GetPattern()
		: nullptr;
	if (!Pattern && EnemyShipArchetype)
	{
		Pattern = EnemyShipArchetype->Pattern;
	}
	FString CastingSummary = TEXT("None");
	FString AbilityDebugText;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		TArray<FString> ActiveAbilityNames;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability)
			{
				continue;
			}
			float Remaining = 0.0f;
			float Duration = 0.0f;
			Spec.Ability->GetCooldownTimeRemainingAndDuration(
				Spec.Handle, ASC->AbilityActorInfo.Get(), Remaining, Duration);
			const FString AbilityName = Spec.Ability->GetAssetTags().IsEmpty()
				? Spec.Ability->GetName()
				: Spec.Ability->GetAssetTags().ToStringSimple();
			const FString AbilityState = Spec.IsActive()
				? TEXT("ACTIVE")
				: Remaining > 0.0f
					? FString::Printf(TEXT("CD %.1f/%.1fs"), Remaining, Duration)
					: TEXT("READY");
			AbilityDebugText += FString::Printf(TEXT("\n- %s: %s"), *AbilityName, *AbilityState);

			if (Spec.IsActive())
			{
				FString ActiveName = AbilityName;
				if (Spec.Ability->GetAssetTags().HasTagExact(GameplayAbility_EnemyShip_Charge))
				{
					ActiveName += ASC->HasMatchingGameplayTag(State_EnemyShip_Charging)
						? TEXT(" [CHARGING]")
						: TEXT(" [AIMING]");
				}
				ActiveAbilityNames.Add(MoveTemp(ActiveName));
			}
		}
		if (!ActiveAbilityNames.IsEmpty())
		{
			CastingSummary = FString::Join(ActiveAbilityNames, TEXT(", "));
		}
	}

	FString DebugText = FString::Printf(
		TEXT("%s [%s]\nCASTING: %s\nNav=%s State=%s Override=%s\nTarget=%s Dist=%s\nPattern=%s Rules=%d"),
		*GetName(),
		HasAuthority() ? TEXT("AUTH") : TEXT("CLIENT"),
		*CastingSummary,
		NavigationComponent->IsNavigationEnabled() ? TEXT("ON") : TEXT("OFF"),
		*StateName,
		NavigationComponent->HasActiveOverride() ? TEXT("YES") : TEXT("NO"),
		TargetShip ? *TargetShip->GetName() : TEXT("None"),
		TargetDistance >= 0.0f ? *FString::Printf(TEXT("%.0fcm"), TargetDistance) : TEXT("-"),
		Pattern ? *Pattern->GetName() : TEXT("None"),
		PatternRuntimeComponent ? PatternRuntimeComponent->GetResolvedRuleCount() : 0);

	if (!AbilityDebugText.IsEmpty())
	{
		DebugText += TEXT("\nAbilities:") + AbilityDebugText;
	}

	int32 ReadyCannons = 0;
	FString CannonReloads;
	for (int32 Index = 0; Index < MountedCannons.Num(); ++Index)
	{
		const ACannon* Cannon = MountedCannons[Index];
		if (!IsValid(Cannon))
		{
			continue;
		}
		const bool bReady = Cannon->CanFireCannon();
		ReadyCannons += bReady ? 1 : 0;
		CannonReloads += FString::Printf(
			TEXT(" #%d:%s"),
			Index,
			bReady ? TEXT("READY") : *FString::Printf(TEXT("%.1fs"), Cannon->GetFireCooldownRemaining()));
	}
	DebugText += FString::Printf(
		TEXT("\nCannons=%d/%d READY%s"), ReadyCannons, MountedCannons.Num(), *CannonReloads);

	DrawDebugString(
		World,
		Center + FVector(0.0f, 0.0f, 350.0f),
		DebugText,
		nullptr,
		FColor::White,
		0.0f,
		true,
		1.0f);
}

void AEnemyShip::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (HealthBarWidgetComponent)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}

	if (!bDeathHandled)
	{
		bDeathHandled = true;
		HandleShipDeath();
	}
}

void AEnemyShip::OnHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
	UpdateHealthBarVisibilityAfterHealthChanged(OldValue, NewValue);
}

void AEnemyShip::OnMaxHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
}

void AEnemyShip::InitializeHealthBarWidget()
{
	if (!HealthBarWidgetComponent)
	{
		return;
	}

	HealthBarWidgetComponent->SetRelativeLocation(HealthBarOffset);
	HealthBarWidgetComponent->SetDrawSize(HealthBarDrawSize);

	if (HealthBarWidgetClass)
	{
		HealthBarWidgetComponent->SetWidgetClass(HealthBarWidgetClass);
	}

	HealthBarWidgetComponent->InitWidget();
	RefreshHealthBarWidget();
	HealthBarWidgetComponent->SetVisibility(HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::AlwaysVisible);
}

void AEnemyShip::RefreshHealthBarWidget()
{
	if (!HealthComponent || !HealthBarWidgetComponent)
	{
		return;
	}

	if (UHealthBarWidget* HealthBarWidget = Cast<UHealthBarWidget>(HealthBarWidgetComponent->GetUserWidgetObject()))
	{
		HealthBarWidget->SetShowHealthText(false);
		HealthBarWidget->SetHealthValues(HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
	}
}

void AEnemyShip::UpdateHealthBarVisibilityAfterHealthChanged(float OldValue, float NewValue)
{
	if (!HealthBarWidgetComponent || HealthBarVisibilityPolicy != EEnemyHealthBarVisibilityPolicy::ShowOnDamage)
	{
		return;
	}

	if (OldValue <= NewValue)
	{
		return;
	}

	HealthBarWidgetComponent->SetVisibility(true);
	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);
	GetWorldTimerManager().SetTimer(HealthBarHideTimerHandle, this, &AEnemyShip::HideHealthBarForDamagePolicy, HealthBarVisibleDurationAfterDamage, false);
}

void AEnemyShip::HideHealthBarForDamagePolicy()
{
	if (HealthBarWidgetComponent && HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::ShowOnDamage)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}
}

void AEnemyShip::HandleShipDeath()
{
	if (!HasAuthority()) return;
	SetAIControlInput(0.0f, 0.0f);
	if (NavigationComponent)
	{
		NavigationComponent->ClearAllOverrides();
		NavigationComponent->SetNavigationEnabled(false);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}

	const FVector DeathLocation = GetActorLocation();
	const FRotator DeathRotation = GetActorRotation();

	// 1. 사망 로그 출력 (이름 + 마지막 체력)
	float FinalHealth = 0.0f;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		FinalHealth = ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	}
	UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::HandleShipDeath - [%s] destroyed! Final Health: %.1f"), *GetName(), FinalHealth);

	// 2. AI Behavior Tree 먼저 정지 (AddForce 경고 방지)
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* BrainComp = AIC->GetBrainComponent())
		{
			BrainComp->StopLogic(TEXT("Ship Destroyed"));
		}
	}

	// 3. BuoyancyCoefficient를 0으로 설정 → 부력만 완전히 제거, 중력으로 자연 침몰
	// Disable the current shared buoyancy source so the network-physics ship sinks.
	if (SWBuoyancyComponent)
	{
		SWBuoyancyComponent->ForceSettings.BuoyancyCoefficient = 0.0f;
	}

	// Keep the legacy component in sync for older derived enemy Blueprints.
	if (UBuoyancyComponent* BuoyancyComp = FindComponentByClass<UBuoyancyComponent>())
	{
		BuoyancyComp->BuoyancyData.BuoyancyCoefficient = 0.0f;
	}

	if (BuoyancyRoot)
	{
		BuoyancyRoot->WakeAllRigidBodies();
	}

	// 4. 대포 발사/조준 타이머 정지
	GetWorldTimerManager().ClearTimer(ActiveCannonsTimerHandle);
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			Cannon->SetAIAimRotation(0.0f, 0.0f);
		}
	}
	ActiveAICannons.Empty();

	DropAtDeathLocation(DeathLocation, DeathRotation);

	// 5. N초 후 Destroy
	GetWorldTimerManager().SetTimer(DeathDestroyTimerHandle, FTimerDelegate::CreateLambda([this]()
	{
		Destroy();
	}), DestroyAfterDeathDelay, false);
}

void AEnemyShip::InitializeEnemyDropData()
{
	// Drop 할 아이템을 Data Table에서 가져오기
	EnemyDropData = FEnemyDropData();
	if (!EnemyDropDataTable || !EnemyTypeTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::InitializeEnemyDropData - Missing drop setup. Ship=%s DropTable=%s EnemyTypeTag=%s"),
			*GetName(),
			*GetNameSafe(EnemyDropDataTable),
			*EnemyTypeTag.ToString());
		return;
	}

	static const FString ContextString(TEXT("EnemyShipDropData"));
	TArray<FEnemyDropDataRow*> Rows;
	EnemyDropDataTable->GetAllRows(ContextString, Rows);

	for (const FEnemyDropDataRow* Row : Rows)
	{
		if (!Row || Row->EnemyTag != EnemyTypeTag)
		{
			continue;
		}

		EnemyDropData.EnemyTag = Row->EnemyTag;
		EnemyDropData.DropEntries = Row->DropEntries;
		UE_LOG(LogTemp, Log, TEXT("AEnemyShip::InitializeEnemyDropData - Loaded %d drop entries. Ship=%s EnemyTypeTag=%s"),
			EnemyDropData.DropEntries.Num(),
			*GetName(),
			*EnemyTypeTag.ToString());
		break;
	}

	if (EnemyDropData.DropEntries.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::InitializeEnemyDropData - No matching drop row or empty drop entries. Ship=%s EnemyTypeTag=%s Table=%s"),
			*GetName(),
			*EnemyTypeTag.ToString(),
			*GetNameSafe(EnemyDropDataTable));
	}
}

void AEnemyShip::DropAtDeathLocation(const FVector& DeathLocation, const FRotator& DeathRotation)
{
	// 죽은 위치에 Storage Spawn하기
	if (!HasAuthority() || bHasDropped)
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - Drop skipped. Ship=%s HasAuthority=%d bHasDropped=%d"),
			*GetName(),
			HasAuthority() ? 1 : 0,
			bHasDropped ? 1 : 0);
		return;
	}
	bHasDropped = true;

	if (!EnemyCorpseStorageClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: EnemyCorpseStorageClass is not configured."), *GetName());
		return;
	}

	// Storage에 들어갈 아이템들의 배열 생성
	TArray<FStorageItemEntry> StorageItems;
	StorageItems.Reserve(EnemyDropData.DropEntries.Num());

	int32 InvalidEntryCount = 0;
	int32 FailedChanceCount = 0;

	// 한 row에 있는 아이템 마다 반복
	for (const FEnemyDropEntry& Entry : EnemyDropData.DropEntries)
	{
		if (!Entry.ItemTag.IsValid())
		{
			++InvalidEntryCount;
			continue;
		}

		const float ClampedChance = FMath::Clamp(Entry.DropChance, 0.f, 1.f);
		// 랜덤으로 뽑은 값이 확률보다 크면 Spawn 하지 않음, Guaranteed면 무조건 Spawn
		if (!Entry.bGuaranteed && FMath::FRand() > ClampedChance)
		{
			++FailedChanceCount;
			continue;
		}

		const int32 MinCount = FMath::Max(1, Entry.MinCount);
		const int32 MaxCount = FMath::Max(MinCount, Entry.MaxCount);

		FStorageItemEntry& StorageItem = StorageItems.AddDefaulted_GetRef();
		StorageItem.ItemTag = Entry.ItemTag;
		StorageItem.Count = FMath::RandRange(MinCount, MaxCount);
	}

	if (StorageItems.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - No storage items selected, so chest will not spawn. Ship=%s EnemyTypeTag=%s Entries=%d Invalid=%d FailedChance=%d"),
			*GetName(),
			*EnemyTypeTag.ToString(),
			EnemyDropData.DropEntries.Num(),
			InvalidEntryCount,
			FailedChanceCount);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - World is null. Ship=%s"), *GetName());
		return;
	}

	const FVector SpawnLocation = DeathLocation + EnemyCorpseStorageSpawnOffset;
	const FRotator SpawnRotation(0.0f, DeathRotation.Yaw, 0.0f);
	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AStorageChest* SpawnedStorage = World->SpawnActor<AStorageChest>(
		EnemyCorpseStorageClass,
		SpawnTransform,
		SpawnParameters
	);

	if (SpawnedStorage)
	{
		SpawnedStorage->SetReplicates(true);
		SpawnedStorage->SetReplicateMovement(true);
		SpawnedStorage->bAlwaysRelevant = true;
		SpawnedStorage->SetNetCullDistanceSquared(FMath::Square(100000.0f));
		SpawnedStorage->SetOwner(nullptr);
		SpawnedStorage->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SpawnedStorage->SetLifeSpan(0.0f);

		TMap<FGameplayTag, int32> TotalCountByItem;
		for (const FStorageItemEntry& StorageItem : StorageItems)
		{
			// map에 아이템 태그랑 개수 추가 
			TotalCountByItem.FindOrAdd(StorageItem.ItemTag) += StorageItem.Count;
		}

		// 앞서 구했던 아이템 개수만큼 슬롯 추가
		int32 RequiredSlotCount = StorageItems.Num();
		if (const UStorageComponent* StorageComponent = SpawnedStorage->GetStorageComponent())
		{
			RequiredSlotCount = 0;
			for (const TPair<FGameplayTag, int32>& ItemTotal : TotalCountByItem)
			{
				// map에 저장된 정보에서, 최대 스택보다 많은 수가 있으면 slot 분할
				const int32 MaxStack = FMath::Max(1, StorageComponent->GetMaxStack(ItemTotal.Key));
				RequiredSlotCount += FMath::DivideAndRoundUp(ItemTotal.Value, MaxStack);
			}
		}

		const int32 SlotCount = FMath::Max(EnemyCorpseStorageSlotCount, RequiredSlotCount);
		SpawnedStorage->ConfigureStorage(SlotCount, EnemyCorpseStorageColumnCount, StorageItems);
		// Replicate the fully configured storage contents in the same server update as the spawn.
		SpawnedStorage->ForceNetUpdate();
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - Spawned storage chest. Ship=%s Chest=%s Location=%s Items=%d Slots=%d"),
			*GetName(),
			*GetNameSafe(SpawnedStorage),
			*SpawnedStorage->GetActorLocation().ToString(),
			StorageItems.Num(),
			SlotCount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - SpawnActor failed. Ship=%s StorageClass=%s Location=%s"),
			*GetName(),
			*GetNameSafe(EnemyCorpseStorageClass),
			*SpawnLocation.ToString());
	}
}

void AEnemyShip::UpdateActiveCannons()
{
	if (!HasAuthority()) return;

	TArray<ACannon*> AvailableCannons;
	AvailableCannons.Reserve(MountedCannons.Num());
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			AvailableCannons.AddUnique(Cannon);
		}
	}

	if (!IsValid(AITargetShip) || AvailableCannons.IsEmpty())
	{
		// 타겟이 없거나 대포가 없으면 활성 대포 정렬을 비우고 기존 대포는 정렬 리셋
		ActiveAICannons.Empty();
		for (ACannon* Cannon : AvailableCannons)
		{
			if (Cannon)
			{
				Cannon->SetAIAimRotation(0.f, 0.f);
			}
		}
		return;
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 타겟 선박과의 거리 기준 정렬 (제곱 거리로 연산 최소화)
	TArray<ACannon*> SortedCannons = MoveTemp(AvailableCannons);
	SortedCannons.Sort([TargetLoc](const ACannon& A, const ACannon& B) {
		float DistA = FVector::DistSquared(A.GetActorLocation(), TargetLoc);
		float DistB = FVector::DistSquared(B.GetActorLocation(), TargetLoc);
		return DistA < DistB;
	});

	ActiveAICannons.Empty();
	const int32 CountToSelect = FMath::Clamp(MaxActiveCannons, 0, SortedCannons.Num());
	for (int32 i = 0; i < CountToSelect; ++i)
	{
		ActiveAICannons.Add(SortedCannons[i]);
	}

	// 활성화되지 못한 나머지 대포들은 조준 초기화(정면 복귀)
	for (ACannon* Cannon : MountedCannons)
	{
		if (Cannon && !ActiveAICannons.Contains(Cannon))
		{
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
	}
}

void AEnemyShip::TickAIAimingAndFiring(float DeltaTime)
{
	if (!AITargetShip || ActiveAICannons.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const float Gravity = FMath::Abs(World->GetGravityZ());
	if (Gravity <= 0.01f)
	{
		return; // 비정상 물리 상태 예외 처리
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 2. 활성 대포별로 각각 조준각 연산 및 발사 진행
	for (ACannon* Cannon : ActiveAICannons)
	{
		if (!IsValid(Cannon)) continue;

		const float ProjectileSpeed = Cannon->GetResolvedFiringStats().ProjectileSpeed;
		if (ProjectileSpeed <= 10.0f)
		{
			Cannon->SetAIAimRotation(0.0f, 0.0f);
			continue;
		}

		FVector StartLoc = Cannon->GetActorLocation();
		FVector ToTarget = TargetLoc - StartLoc;

		float HorizDist = FVector::Dist2D(StartLoc, TargetLoc);
		float VertDist = ToTarget.Z;

		// 3. 탄도학 투사 궤적 공식 대입 (해석학적 공식)
		// Disc = v^4 - g * (g * x^2 + 2 * y * v^2)
		float SpeedSq = ProjectileSpeed * ProjectileSpeed;
		float Speed4 = SpeedSq * SpeedSq;
		float Disc = Speed4 - Gravity * (Gravity * HorizDist * HorizDist + 2.f * VertDist * SpeedSq);

		if (Disc < 0.f)
		{
			// 최대 사거리를 벗어난 경우 조준을 풀고 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
			continue;
		}

		// 저각 탄도 계산
		float PitchRad = FMath::Atan2(SpeedSq - FMath::Sqrt(Disc), Gravity * HorizDist);
		// 월드 공간 발사 방향 벡터 생성
		FVector HorizDir = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal();
		FVector LaunchDir = HorizDir * FMath::Cos(PitchRad) + FVector(0.f, 0.f, FMath::Sin(PitchRad));

		// 대포의 로컬 공간으로 변환하여 Yaw / Pitch 도출
		FVector LocalLaunchDir = Cannon->GetActorTransform().InverseTransformVector(LaunchDir);
		FRotator TargetRot = LocalLaunchDir.Rotation();

		float TargetPitch = TargetRot.Pitch;
		float TargetYaw = TargetRot.Yaw;

		// 4. 180도 고개 돌림 방지 체크 (로컬 Yaw가 좌우 90도를 초과하면 조준 불가 상태 처리)
		if (FMath::Abs(TargetYaw) > 90.f)
		{
			// 조준하지 않고 정면 정렬 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
		else
		{
			// 조준 제어 적용
			Cannon->SetAIAimRotation(TargetPitch, TargetYaw);

			// 선회(Orbit) 또는 도망(Retreat) 상태 시 지속 발사
			if (CurrentCombatState == ENavalCombatState::Orbit || CurrentCombatState == ENavalCombatState::Retreat)
			{
				Cannon->FireCannon();
			}
		}
	}
}
