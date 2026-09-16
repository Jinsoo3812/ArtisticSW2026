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
#include "AI/EnemyTerritoryComponent.h"
#include "GAS/EnemyAttributeSet.h"
#include "EngineUtils.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AISense_Damage.h"
#include "UI/EnemyHealthBarComponent.h"

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

	// Prevent player camera boom from clipping / zooming against enemies
	if (UCapsuleComponent* CapsuleComp = GetCapsuleComponent())
	{
		CapsuleComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	
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
	TerritoryComponent = CreateDefaultSubobject<UEnemyTerritoryComponent>(TEXT("TerritoryComponent"));
	// All regular enemy archetypes share this confirmed-damage cue. Specialized
	// enemies must opt into a different cue in their own constructor.
	HealthComponent->SetDamageGameplayCueTag(GameplayCue_Enemy_Hit);

	// ================= Health Bar =================
	EnemyHealthBarComponent = CreateDefaultSubobject<UEnemyHealthBarComponent>(TEXT("EnemyHealthBarComponent"));
	EnemyHealthBarComponent->SetupAttachment(GetRootComponent());
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

	if (const UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		BaseMovementSpeed = FMath::Max(0.0f, MovementComponent->MaxWalkSpeed);
	}

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		if (HasAuthority() && !ApplyBaseStatsForSpawn())
		{
			SetActorEnableCollision(false);
			Destroy();
			return;
		}
		BindMovementSpeedAttribute();
		if (HasAuthority())
		{
			AbilitySystemComponent->AddLooseGameplayTag(Team_Enemy);
			AbilitySystemComponent->AddLooseGameplayTag(Capability_Effect_MoveSpeedMultiplier);
			AbilitySystemComponent->AddLooseGameplayTag(Capability_Effect_AttackSpeedMultiplier);
			AbilitySystemComponent->AddLooseGameplayTags(EffectTargetTags);
			if (EnemyTypeTag.IsValid())
			{
				AbilitySystemComponent->AddLooseGameplayTag(EnemyTypeTag);
			}
		}
		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseEnemy::OnDeathStarted);
			HealthComponent->OnDeathFinished.AddUniqueDynamic(this, &ABaseEnemy::OnDeathFinished);
			HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &ABaseEnemy::OnHealthChanged);
			HealthComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
		}
	}

	if (EnemyHealthBarComponent)
	{
		EnemyHealthBarComponent->ConfigurePresentation(HealthBarOffset, HealthBarDrawSize);
		EnemyHealthBarComponent->SetVisibilitySourceComponent(GetMesh());
	}
	if (HasAuthority())
	{
		SetBaseMovementSpeed(BaseMovementSpeed);
	}

	// StartingAbilities 능력 등록. Death GA는 사망 파이프라인에서 하나만
	// 실행되어야 하므로 전용 설정으로 정규화합니다.
	if (AbilitySystemComponent && HasAuthority())
	{
		TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant = StartingAbilities;
		AbilitiesToGrant.RemoveAll([this](const TSubclassOf<UGameplayAbility>& AbilityClass)
		{
			return AbilityClass
				&& AbilityClass->IsChildOf(UBaseDeathGameplayAbility::StaticClass())
				&& AbilityClass.Get() != DeathAbilityClass.Get();
		});
		if (DeathAbilityClass)
		{
			AbilitiesToGrant.AddUnique(TSubclassOf<UGameplayAbility>(DeathAbilityClass.Get()));
		}
		GrantAbilities(AbilitiesToGrant);
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
	UnbindMovementSpeedAttribute();

	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseEnemy::OnDeathStarted);
		HealthComponent->OnDeathFinished.RemoveDynamic(this, &ABaseEnemy::OnDeathFinished);
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &ABaseEnemy::OnHealthChanged);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	Super::EndPlay(EndPlayReason);
}

void ABaseEnemy::Destroyed()
{
	// Weapon actors are independently replicated. Actor ownership controls
	// relevancy, not lifetime, so permanently removing an enemy must explicitly
	// remove its loadout on the authority before the owner channel closes.
	if (HasAuthority() && WeaponComponent)
	{
		WeaponComponent->DestroyCurrentWeapon();
	}

	Super::Destroyed();
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
	// DeathStarted에서 실행되는 즉시 게임플레이 정리 훅입니다.
}

bool ABaseEnemy::ShouldWaitForDeathAbility() const
{
	return DeathAbilityClass != nullptr;
}

void ABaseEnemy::HandleDeathFinishedPresentation()
{
	ApplyLocalDeathRagdoll();
}

void ABaseEnemy::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (!bDeathHandled)
	{
		bDeathHandled = true;

		if (HasAuthority())
		{
			if (WeaponComponent)
			{
				WeaponComponent->DeactivateForOwnerDeath();
			}
			NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason::Death);
			Drop();
		}

		HandleDeath();

		// Regular enemies do not own a death GA. Their physical death presentation
		// begins immediately, while montage-driven enemies (Boss) wait for
		// UBaseHealthComponent::FinishDeath.
		if (!ShouldWaitForDeathAbility())
		{
			ApplyLocalDeathRagdoll();
		}
	}
}

void ABaseEnemy::OnDeathFinished(UBaseHealthComponent* InHealthComponent)
{
	HandleDeathFinishedPresentation();

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

void ABaseEnemy::BindMovementSpeedAttribute()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	if (!MoveSpeedBonusChangedDelegateHandle.IsValid())
	{
		MoveSpeedBonusChangedDelegateHandle = AbilitySystemComponent
			->GetGameplayAttributeValueChangeDelegate(UEnemyAttributeSet::GetMoveSpeedBonusAttribute())
			.AddUObject(this, &ABaseEnemy::OnMovementSpeedModifierChanged);
	}
	if (!MoveSpeedMultiplierChangedDelegateHandle.IsValid())
	{
		MoveSpeedMultiplierChangedDelegateHandle = AbilitySystemComponent
			->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute())
			.AddUObject(this, &ABaseEnemy::OnMovementSpeedModifierChanged);
	}
}

void ABaseEnemy::UnbindMovementSpeedAttribute()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	if (MoveSpeedBonusChangedDelegateHandle.IsValid())
	{
		AbilitySystemComponent
			->GetGameplayAttributeValueChangeDelegate(UEnemyAttributeSet::GetMoveSpeedBonusAttribute())
			.Remove(MoveSpeedBonusChangedDelegateHandle);
		MoveSpeedBonusChangedDelegateHandle.Reset();
	}
	if (MoveSpeedMultiplierChangedDelegateHandle.IsValid())
	{
		AbilitySystemComponent
			->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute())
			.Remove(MoveSpeedMultiplierChangedDelegateHandle);
		MoveSpeedMultiplierChangedDelegateHandle.Reset();
	}
}

void ABaseEnemy::OnMovementSpeedModifierChanged(const FOnAttributeChangeData& ChangeData)
{
	// Enemy movement is server-authored. Replicated attributes still reach clients
	// for UI/cues, but simulated proxies follow CharacterMovement replication.
	if (HasAuthority())
	{
		SetBaseMovementSpeed(BaseMovementSpeed);
	}
}

void ABaseEnemy::SetBaseMovementSpeed(float NewBaseSpeed)
{
	if (!HasAuthority())
	{
		return;
	}

	BaseMovementSpeed = FMath::Max(0.0f, NewBaseSpeed);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = GetResolvedMovementSpeed();
	}
}

float ABaseEnemy::GetResolvedMovementSpeed() const
{
	const float MoveSpeedBonus = AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UEnemyAttributeSet::GetMoveSpeedBonusAttribute())
		: 0.0f;
	const float MoveSpeedMultiplier = AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute())
		: 1.0f;
	return ResolveMovementSpeed(
		BaseMovementSpeed,
		SpawnMovementSpeedMultiplier,
		MoveSpeedBonus,
		MaximumResolvedMovementSpeed,
		MoveSpeedMultiplier);
}

float ABaseEnemy::ResolveMovementSpeed(
	float InBaseSpeed,
	float InSpawnMultiplier,
	float InMoveSpeedBonus,
	float InMaximumSpeed,
	float InMoveSpeedMultiplier)
{
	const float SafeBaseSpeed = FMath::Max(0.0f, InBaseSpeed);
	if (SafeBaseSpeed <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float BuffedSpeed = SafeBaseSpeed * FMath::Max(0.01f, InSpawnMultiplier)
		+ FMath::Max(0.0f, InMoveSpeedBonus);
	const float ResolvedSpeed = BuffedSpeed * FMath::Clamp(InMoveSpeedMultiplier, 0.1f, 3.0f);
	return FMath::Clamp(ResolvedSpeed, 0.0f, FMath::Max(0.0f, InMaximumSpeed));
}

void ABaseEnemy::InitializeFromWaveSpawn(float HealthMultiplier, float SpeedMultiplier, int32 EnemyLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	// Legacy BP callers may configure before BeginPlay. The wave manager now does so
	// through ConfigureSpawnBalance before FinishSpawning. Never heal a live enemy here.
	if (!bBalanceApplied)
	{
		ConfigureSpawnBalance(SpawnStatsRow, HealthMultiplier, SpeedMultiplier);
	}
}

bool ABaseEnemy::ConfigureSpawnBalance(const FDataTableRowHandle& Row, float HealthMultiplier, float SpeedMultiplier)
{
	if (!HasAuthority() || bBalanceApplied || !FMath::IsFinite(HealthMultiplier) || HealthMultiplier <= 0.f
		|| !FMath::IsFinite(SpeedMultiplier) || SpeedMultiplier <= 0.f)
	{
		return false;
	}
	SpawnStatsRow = Row;
	SpawnHealthMultiplier = HealthMultiplier;
	SpawnMovementSpeedMultiplier = SpeedMultiplier;
	return true;
}

bool ABaseEnemy::ConfigureSpawnTypeTag(FGameplayTag InEnemyTypeTag)
{
	if (!HasAuthority() || HasActorBegunPlay() || !InEnemyTypeTag.IsValid())
	{
		return false;
	}

	EnemyTypeTag = InEnemyTypeTag;
	return true;
}

void ABaseEnemy::ResetBalanceForReuse()
{
	if (!HasAuthority()) return;
	bBalanceApplied = false;
	bBalanceReady = false;
	SpawnStatsRow = FDataTableRowHandle();
	SpawnHealthMultiplier = 1.f;
	SpawnMovementSpeedMultiplier = 1.f;
	BalancedAttackInterval = 0.f;
	BalancedMeleeAttackerLimit = 0;
	BalanceAttackReadyTime = 0.;
}

bool ABaseEnemy::ApplyBaseStatsForSpawn()
{
	if (!HasAuthority() || !AbilitySystemComponent || !BasicAttributes) return false;
	if (bBalanceApplied) return bBalanceReady;
	const FDataTableRowHandle& Selection = SpawnStatsRow.IsNull() ? DefaultStatsRow : SpawnStatsRow;
	FEnemyBaseStatsRow Values;
	FEnemyCombatBalanceRow Combat;
	if (!Selection.IsNull())
	{
		const auto* Row = Selection.GetRow<FEnemyBaseStatsRow>(TEXT("Enemy spawn balance"));
		if (!Row || !Row->IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("[EnemyBalance] Invalid stats: Enemy=%s Row=%s"), *GetName(), *Selection.RowName.ToString());
			return false;
		}
		Values = *Row;
		if (!Values.CombatSettings.IsNull())
		{
			const auto* CombatRow = Values.CombatSettings.GetRow<FEnemyCombatBalanceRow>(TEXT("Enemy combat balance"));
			if (!CombatRow || !CombatRow->IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("[EnemyBalance] Invalid combat row for %s"), *GetName());
				return false;
			}
			Combat = *CombatRow;
		}
	}
	else
	{
		// Existing unconfigured enemies retain their authored defaults.
		Values.MaxHealth = BasicAttributes->GetMaxHealth();
		Values.Strength = AbilitySystemComponent->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute());
		Values.MoveSpeedMultiplier = BasicAttributes->GetMoveSpeedMultiplier();
		Values.AttackSpeedMultiplier = BasicAttributes->GetAttackSpeedMultiplier();
	}
	const float MaxHealth = Values.MaxHealth * SpawnHealthMultiplier;
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f) return false;
	AbilitySystemComponent->SetNumericAttributeBase(UBaseAttributeSet::GetMaxHealthAttribute(), MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), Values.Strength);
	AbilitySystemComponent->SetNumericAttributeBase(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute(), Values.MoveSpeedMultiplier);
	AbilitySystemComponent->SetNumericAttributeBase(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute(), Values.AttackSpeedMultiplier);
	AbilitySystemComponent->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), MaxHealth);
	BalancedAttackInterval = Combat.AttackInterval;
	BalancedMeleeAttackerLimit = Combat.MaxSimultaneousMeleeAttackers;
	BalanceAttackReadyTime = GetWorld()->GetTimeSeconds() + Combat.InitialAttackDelay;
	bBalanceReady = bBalanceApplied = true;
	SetBaseMovementSpeed(BaseMovementSpeed);
	UE_LOG(LogTemp, Log, TEXT("[EnemyBalance] Enemy=%s Row=%s MaxHealth=%.2f StrengthBase=%.2f Interval=%.2f"),
		*GetName(), *Selection.RowName.ToString(), MaxHealth, Values.Strength, BalancedAttackInterval);
	return true;
}

float ABaseEnemy::GetBalancedAttackInterval(float Fallback) const
{
	return BalancedAttackInterval > 0.f ? BalancedAttackInterval : Fallback;
}

bool ABaseEnemy::IsBalanceAttackReady() const
{
	const bool bLegacyUnconfigured = DefaultStatsRow.IsNull() && SpawnStatsRow.IsNull();
	return (bBalanceReady || bLegacyUnconfigured) && GetWorld() && GetWorld()->GetTimeSeconds() >= BalanceAttackReadyTime;
}

bool ABaseEnemy::HasBalancedMeleeAttackSlot() const
{
	if (BalancedMeleeAttackerLimit <= 0) return true;
	const ABaseAIController* OwningController = Cast<ABaseAIController>(GetController());
	const AActor* Target = OwningController ? OwningController->GetCombatTarget() : nullptr;
	if (!Target) return false;
	int32 Attackers = 0;
	// Evaluated only at attack start on the server, never per tick. Each committed
	// attack owns State.Attacking until its normal end or cancellation.
	for (TActorIterator<ABaseEnemy> It(GetWorld()); It; ++It)
	{
		const ABaseEnemy* Other = *It;
		const ABaseAIController* OtherController = Cast<ABaseAIController>(Other->GetController());
		if (Other != this && Other->BalancedMeleeAttackerLimit > 0 && OtherController
			&& OtherController->GetCombatTarget() == Target && Other->GetAbilitySystemComponent()
			&& Other->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Attacking)
			&& ++Attackers >= BalancedMeleeAttackerLimit) return false;
	}
	return true;
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
