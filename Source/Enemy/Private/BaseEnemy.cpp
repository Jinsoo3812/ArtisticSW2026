// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"
#include "Weapon/BaseWeaponComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"

#include "Storage/StorageChest.h"

// Enemy Folder
#include "AI/BaseAIController.h"
#include "GAS/EnemyAttributeSet.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/WidgetComponent.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AISense_Damage.h"
#include "UI/HealthBarWidget.h"
#include "UObject/ConstructorHelpers.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	bUseControllerRotationYaw = true;

	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(5.0f);
	SetNetCullDistanceSquared(FMath::Square(15000.0f));
	
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	
	// ASC
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	AbilitySystemComponent->SetReplicationMode(ASCReplicationMode);
	
	// GAS
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));

	// Component
	WeaponComponent = CreateDefaultSubobject<UBaseWeaponComponent>(TEXT("WeaponComponent"));
	WaypointMoveComponent = CreateDefaultSubobject<UEnemyWaypointMoveComponent>(TEXT("WaypointMoveComponent"));
	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));

	// ================= Health Bar =================
	HealthBarWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidgetComponent"));
	HealthBarWidgetComponent->SetupAttachment(GetRootComponent());
	HealthBarWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidgetComponent->SetDrawAtDesiredSize(false);
	HealthBarWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FClassFinder<UHealthBarWidget> HealthBarWidgetFinder(TEXT("/Game/Blueprints/02_UI/UI_HUD/WBP_HealthBarWidget"));
	if (HealthBarWidgetFinder.Succeeded())
	{
		HealthBarWidgetClass = HealthBarWidgetFinder.Class;
		HealthBarWidgetComponent->SetWidgetClass(HealthBarWidgetClass);
	}
	// ================= End of Health Bar =================

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->SetIsReplicated(true);

		// Enemies may stand on physics-driven ship decks. CharacterMovement's
		// default push/touch forces feed back into the ship body and cause jitter.
		MovementComponent->bOrientRotationToMovement = false;
		MovementComponent->bEnablePhysicsInteraction = false;
		MovementComponent->bTouchForceScaledToMass = false;
		MovementComponent->InitialPushForceFactor = 0.0f;
		MovementComponent->PushForceFactor = 0.0f;
		MovementComponent->TouchForceFactor = 0.0f;
	}
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		if (HasAuthority())
		{
			AbilitySystemComponent->AddLooseGameplayTag(Team_Enemy);
		}
		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseEnemy::OnDeathStarted);
			HealthComponent->OnDeathFinished.AddUniqueDynamic(this, &ABaseEnemy::OnDeathFinished);
			HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &ABaseEnemy::OnHealthChanged);
			HealthComponent->OnMaxHealthChanged.AddUniqueDynamic(this, &ABaseEnemy::OnMaxHealthChanged);
			HealthComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
		}
	}

	InitializeHealthBarWidget();

	// StartingAbilities 능력 등록
	if (AbilitySystemComponent && HasAuthority())
	{
		if (StartingAbilities.Num() > 0)
		{
			GrantAbilities(StartingAbilities);
		}
		// 무기 관리
		if (WeaponComponent && DefaultWeaponTag.IsValid())
		{
			if (bEquipWeaponOnSpawn)
			{
				WeaponComponent->InitializeLoadout(DefaultWeaponTag);
			}
			else
			{
				WeaponComponent->InitializeHolsteredLoadout(DefaultWeaponTag);
			}
		}
	}

	InitializeEnemyDropData();
}

void ABaseEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseEnemy::OnDeathStarted);
		HealthComponent->OnDeathFinished.RemoveDynamic(this, &ABaseEnemy::OnDeathFinished);
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &ABaseEnemy::OnHealthChanged);
		HealthComponent->OnMaxHealthChanged.RemoveDynamic(this, &ABaseEnemy::OnMaxHealthChanged);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);

	Super::EndPlay(EndPlayReason);
}

TArray<FGameplayAbilitySpecHandle> ABaseEnemy::GrantAbilities(TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant)
{
	// 모든 능력을 for loop를 통해서 일일히 Grant 해줌
	// HasAuthority는 서버에 있는 지 확인하는 함수
	if (!AbilitySystemComponent || !HasAuthority())
		// GrantAbilities는 서버에서만 동작하므로, 서버에서 클라로 보내는 것은 충돌 일어날 수 있다. 따라서 서버에서만 동작하도록 한다.
	{
		return TArray<FGameplayAbilitySpecHandle>();
	}

	TArray<FGameplayAbilitySpecHandle> AbilitiesHandles;
	
	for (TSubclassOf<UGameplayAbility> Ability : AbilitiesToGrant)
	{
		if (!Ability)
			continue;
		
		FGameplayAbilitySpecHandle SpecHandle= AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec
			(Ability, 1, -1, this));
		
		AbilitiesHandles.Add(SpecHandle);
	}

	// SendAbilitiesChangedEvent();
	return AbilitiesHandles;
}


void ABaseEnemy::NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason Reason)
{
	if (!HasAuthority() || bWaveRemoveNotified)
	{
		return;
	}

	bWaveRemoveNotified = true;

	if (WaypointMoveComponent)
	{
		WaypointMoveComponent->StopRoute(true);
	}

	OnBaseEnemyDeathNotified.Broadcast(this, Reason);
}

void ABaseEnemy::HandleDeath_Implementation()
{
	ApplyLocalDeathRagdoll();
}

void ABaseEnemy::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (HealthBarWidgetComponent)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}

	if (!bDeathHandled)
	{
		bDeathHandled = true;

		if (HasAuthority())
		{
			NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason::Death);
			Drop();
		}

		HandleDeath();
	}
}

void ABaseEnemy::OnDeathFinished(UBaseHealthComponent* InHealthComponent)
{
	if (!HasAuthority() || !bDestroyAfterDeathFinished || IsActorBeingDestroyed())
	{
		return;
	}

	if (CorpseLifetimeAfterDeathFinished <= 0.0f)
	{
		Destroy();
		return;
	}

	SetLifeSpan(CorpseLifetimeAfterDeathFinished);
}

void ABaseEnemy::OnHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
	UpdateHealthBarVisibilityAfterHealthChanged(OldValue, NewValue);

	// GAS attribute changes do not automatically create an AI Damage stimulus.
	// Report only authoritative, real health loss and keep synthetic Player input out of production code.
	if (HasAuthority() && OldValue > NewValue && IsValid(InstigatorActor) && InstigatorActor != this)
	{
		const FVector DamageLocation = GetActorLocation();
		UAISense_Damage::ReportDamageEvent(
			this,
			this,
			InstigatorActor,
			OldValue - NewValue,
			DamageLocation,
			DamageLocation);
	}
}

void ABaseEnemy::OnMaxHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
}

bool ABaseEnemy::CanEngageActor_Implementation(AActor* Candidate) const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(Candidate);
	if (!IsValid(Player) || Player->IsActorBeingDestroyed())
	{
		return false;
	}

	if (const UBaseHealthComponent* TargetHealth = Player->FindComponentByClass<UBaseHealthComponent>())
	{
		return !TargetHealth->IsDead();
	}

	return true;
}

void ABaseEnemy::InitializeHealthBarWidget()
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

void ABaseEnemy::RefreshHealthBarWidget()
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

void ABaseEnemy::UpdateHealthBarVisibilityAfterHealthChanged(float OldValue, float NewValue)
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
	GetWorldTimerManager().SetTimer(HealthBarHideTimerHandle, this, &ABaseEnemy::HideHealthBarForDamagePolicy, HealthBarVisibleDurationAfterDamage, false);
}

void ABaseEnemy::HideHealthBarForDamagePolicy()
{
	if (HealthBarWidgetComponent && HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::ShowOnDamage)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}
}

void ABaseEnemy::InitializeFromWaveSpawn(float HealthMultiplier, float SpeedMultiplier, int32 EnemyLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed *= FMath::Max(0.01f, SpeedMultiplier);
	}

	// 이후 AttributeSet 또는 GameplayEffect를 사용하여
	// HealthMultiplier와 EnemyLevel을 실제 스탯에 반영
}

void ABaseEnemy::InitializeEnemyDropData()
{
	// 데이터 테이블 전체를 가져옴
	EnemyDropData = FEnemyDropData();
	if (!EnemyDropDataTable || !EnemyTypeTag.IsValid())
	{
		return;
	}

	static const FString ContextString(TEXT("EnemyDropData"));
	TArray<FEnemyDropDataRow*> Rows;
	EnemyDropDataTable->GetAllRows(ContextString, Rows);

	// 전체 데이터중 해당하는 Row 검색 후 데이터 가져옴
	for (const FEnemyDropDataRow* Row : Rows)
	{
		if (!Row || Row->EnemyTag != EnemyTypeTag)
		{
			continue;
		}
		// 구조체 Tag랑 BaseEnemy TypeTag가 같을 때 아래 실행

		EnemyDropData.EnemyTag = Row->EnemyTag;
		EnemyDropData.DropEntries = Row->DropEntries;
		break;
	}
}

void ABaseEnemy::Drop()
{
	if (!HasAuthority() || bHasDropped)
	{
		return;
	}
	bHasDropped = true;

	if (!EnemyCorpseStorageClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: EnemyCorpseStorageClass is not configured."), *GetName());
		return;
	}

	TArray<FStorageItemEntry> StorageItems;
	StorageItems.Reserve(EnemyDropData.DropEntries.Num());

	for (const FEnemyDropEntry& Entry : EnemyDropData.DropEntries)
	{
		if (!Entry.ItemTag.IsValid())
		{
			continue;
		}

		const float ClampedChance = FMath::Clamp(Entry.DropChance, 0.f, 1.f);
		if (!Entry.bGuaranteed && FMath::FRand() > ClampedChance)
		{
			continue;
		}

		const int32 MinCount = FMath::Max(1, Entry.MinCount);
		const int32 MaxCount = FMath::Max(MinCount, Entry.MaxCount);

		FStorageItemEntry& StorageItem = StorageItems.AddDefaulted_GetRef();
		StorageItem.ItemTag = Entry.ItemTag;
		StorageItem.Count = FMath::RandRange(MinCount, MaxCount);
	}

	// 당첨된 아이템이 하나도 없으면 빈 Storage는 생성하지 않는다.
	if (StorageItems.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AStorageChest* SpawnedStorage = World->SpawnActor<AStorageChest>(
		EnemyCorpseStorageClass,
		SpawnTransform,
		SpawnParameters
	);

	if (SpawnedStorage)
	{
		TMap<FGameplayTag, int32> TotalCountByItem;
		for (const FStorageItemEntry& StorageItem : StorageItems)
		{
			TotalCountByItem.FindOrAdd(StorageItem.ItemTag) += StorageItem.Count;
		}

		int32 RequiredSlotCount = StorageItems.Num();
		if (const UStorageComponent* StorageComponent = SpawnedStorage->GetStorageComponent())
		{
			RequiredSlotCount = 0;
			for (const TPair<FGameplayTag, int32>& ItemTotal : TotalCountByItem)
			{
				const int32 MaxStack = FMath::Max(1, StorageComponent->GetMaxStack(ItemTotal.Key));
				RequiredSlotCount += FMath::DivideAndRoundUp(ItemTotal.Value, MaxStack);
			}
		}

		const int32 SlotCount = FMath::Max(EnemyCorpseStorageSlotCount, RequiredSlotCount);
		SpawnedStorage->ConfigureStorage(SlotCount, EnemyCorpseStorageColumnCount, StorageItems);
	}
}
