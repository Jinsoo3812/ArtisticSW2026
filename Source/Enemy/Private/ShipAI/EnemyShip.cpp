// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/EnemyShip.h"
#include "Cannon.h"
#include "AbilitySystemComponent.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
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

AEnemyShip::AEnemyShip()
{
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
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

	// 캐싱된 대포 목록 탐색
	// Drop에 관한 정보 초기화
	InitializeEnemyDropData();

	// 0.5초마다 타겟과 가장 가까운 N개의 대포를 선정해 목록을 갱신하는 타이머 작동
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ActiveCannonsTimerHandle, this, &AEnemyShip::UpdateActiveCannons, 0.5f, true);
	}

	// 군집 서브시스템에 등록
	if (HasAuthority())
	{
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

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && !bDeathHandled)
	{
		TickAIAimingAndFiring(DeltaTime);
	}
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
