// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageChest.h"
#include "BaseGameplayTags.h"
#include "Balance/ProgressionBalanceData.h"
#include "Item/ItemData.h"
#include "BaseCharacter.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "InteractableComponent.h"
#include "CollisionChannels.h"
#include "Storage/StorageInteractionDiagnostics.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AStorageChest::AStorageChest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetCullDistanceSquared(FMath::Square(500000.0f));
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	ChestMesh->SetCollisionProfileName(TEXT("StorageChest"));
	ChestMesh->SetGenerateOverlapEvents(false);
	SetRootComponent(ChestMesh);

	// Keep the old native component name so derived Blueprints can conform their
	// serialized hierarchy, but make it a child of the physics root.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetupAttachment(ChestMesh);

	SWBuoyancyComponent = CreateDefaultSubobject<USWBuoyancyComponent>(TEXT("SWBuoyancyComponent"));
	SWBuoyancyComponent->ExecutionMode = ESWBuoyancyExecutionMode::ServerAuthority;
	SWBuoyancyComponent->ConfigureSinglePontoon(50.0f);
	// Preserve the Water plugin's near-surface coefficient while accelerating only
	// the fully submerged recovery after a large fall.
	SWBuoyancyComponent->ForceSettings.DeepWaterBuoyancyMultiplier = 3.0f;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(ChestMesh);
	InteractableComponent->InteractableIdTag = Interactable_Id_StorageChest;

	StorageComponent = CreateDefaultSubobject<UStorageComponent>(TEXT("StorageComponent"));
}

void AStorageChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() || !bEnablePhysicsAndBuoyancy || !bHasClientMovementTarget)
	{
		return;
	}

	const float TimeSinceUpdate = GetWorld()
		? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - ClientMovementTargetReceiveTime)
		: 0.0f;
	const float ExtrapolationTime = FMath::Min(TimeSinceUpdate, ClientMaxExtrapolationTime);
	const FVector DesiredLocation =
		ClientMovementTargetLocation + ClientMovementTargetVelocity * ExtrapolationTime;

	if (FVector::DistSquared(GetActorLocation(), DesiredLocation)
		> FMath::Square(ClientNetworkSnapDistance))
	{
		SetActorLocationAndRotation(
			DesiredLocation,
			ClientMovementTargetRotation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		return;
	}

	const FVector SmoothedLocation = FMath::VInterpTo(
		GetActorLocation(),
		DesiredLocation,
		DeltaSeconds,
		ClientLocationInterpSpeed);
	const FQuat SmoothedRotation = FMath::QInterpTo(
		GetActorQuat(),
		ClientMovementTargetRotation,
		DeltaSeconds,
		ClientRotationInterpSpeed);

	SetActorLocationAndRotation(
		SmoothedLocation,
		SmoothedRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AStorageChest::BeginPlay()
{
	Super::BeginPlay();
	// The mesh is physical cover, not an interaction target. Blueprint-saved
	// collision overrides must not block the sweep before it reaches the sphere.
	if (ChestMesh)
	{
		ChestMesh->SetCollisionResponseToChannel(ECC_Interactable, ECR_Ignore);
	}

	if (HasAuthority() && ChestDefinition && !bDefinitionInitialized)
	{
		InitializeFromChestDefinition(ChestDefinition, LootSeed);
	}

	ApplyPhysicsMode();
	RefreshDistanceOptimizationTimer();

	if (InteractableComponent)
	{
		InteractableComponent->OnInteracted.AddUniqueDynamic(this, &AStorageChest::HandleInteracted);
	}

	if (HasAuthority())
	{
		InitializeGuardState();
		if (StorageComponent)
		{
			StorageComponent->OnStorageChanged.AddUObject(this, &AStorageChest::HandleStorageChanged);
		}
	}

	ApplyLockPresentation();
}

void AStorageChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearGuardBindings();

	if (StorageComponent)
	{
		StorageComponent->OnStorageChanged.RemoveAll(this);
	}
	GetWorldTimerManager().ClearTimer(EmptyDestroyTimerHandle);
	GetWorldTimerManager().ClearTimer(DistanceOptimizationTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AStorageChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStorageChest, bLocked);
	DOREPLIFETIME(AStorageChest, bGuardFailed);
	DOREPLIFETIME(AStorageChest, bEnablePhysicsAndBuoyancy);
	DOREPLIFETIME(AStorageChest, bDistanceOptimizationDormant);
}

void AStorageChest::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
	if (StorageComponent)
	{
		StorageComponent->ConfigureStorage(InSlotCount, InColumnCount, InItems);
	}
}

void AStorageChest::SetPhysicsAndBuoyancyEnabled(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	bEnablePhysicsAndBuoyancy = bEnabled;
	if (HasActorBegunPlay())
	{
		if (!bEnabled && bDistanceOptimizationDormant)
		{
			SetDistanceOptimizationDormant(false);
		}
		ApplyPhysicsMode();
		RefreshDistanceOptimizationTimer();
	}
}

void AStorageChest::SetDistanceOptimizationEnabled(bool bEnabled)
{
	if (!HasAuthority() || bEnableDistanceOptimization == bEnabled)
	{
		return;
	}
	if (!bEnabled && bDistanceOptimizationDormant)
	{
		SetDistanceOptimizationDormant(false);
	}
	bEnableDistanceOptimization = bEnabled;
	if (HasActorBegunPlay())
	{
		RefreshDistanceOptimizationTimer();
	}
}

void AStorageChest::InitializeFromChestDefinition(UChestDefinition* InDefinition, int32 Seed, float ExpectedValueRatio)
{
	if (!HasAuthority() || !InDefinition)
	{
		return;
	}

	ChestDefinition = InDefinition;
	LootSeed = Seed;
	ConfigureStorage(
		FMath::Max(1, InDefinition->GetEffectiveSlotCount()),
		FMath::Max(1, InDefinition->GetEffectiveColumnCount()),
		InDefinition->RollInitialItems(Seed, ExpectedValueRatio));
	bDefinitionInitialized = true;

	if (HasActorBegunPlay())
	{
		ApplyPhysicsMode();
		RefreshDistanceOptimizationTimer();
	}
}

void AStorageChest::ConfigureGuarding(
	bool bInRequiresGuardClear,
	const TArray<ABaseCharacter*>& InGuardCharacters,
	AShip* InOwningShip)
{
	if (!HasAuthorityOrIsTesting())
	{
		return;
	}

	bRequiresGuardClear = bInRequiresGuardClear;
	GuardCharacters.Reset(InGuardCharacters.Num());
	for (ABaseCharacter* GuardCharacter : InGuardCharacters)
	{
		if (GuardCharacter)
		{
			GuardCharacters.AddUnique(GuardCharacter);
		}
	}
	OwningShip = InOwningShip;

	if (OwningShip)
	{
		bEnablePhysicsAndBuoyancy = false;
		if (ChestMesh)
		{
			ChestMesh->IgnoreActorWhenMoving(OwningShip, true);
			ChestMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			ChestMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
		}
	}

	// Configure immediately as well as in BeginPlay. Deferred spawns therefore
	// enter the world already locked, and placed/runtime reconfiguration is deterministic.
	InitializeGuardState();

	if (HasActorBegunPlay())
	{
		ApplyPhysicsMode();
		RefreshDistanceOptimizationTimer();
	}
}

void AStorageChest::AddGuardCharacter(ABaseCharacter* NewGuard)
{
	if (!HasAuthorityOrIsTesting() || !IsValid(NewGuard))
	{
		return;
	}

	GuardCharacters.AddUnique(NewGuard);
	bRequiresGuardClear = true;

	UBaseHealthComponent* GuardHealth = NewGuard->FindComponentByClass<UBaseHealthComponent>();
	if (!GuardHealth)
	{
		for (UActorComponent* Comp : NewGuard->GetInstanceComponents())
		{
			if (UBaseHealthComponent* CastHealth = Cast<UBaseHealthComponent>(Comp))
			{
				GuardHealth = CastHealth;
				break;
			}
		}
	}
	if (GuardHealth && !GuardHealth->IsDead())
	{
		AliveGuardHealthComponents.Add(GuardHealth);
		GuardHealth->OnDeathStarted.AddUniqueDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
		SetLocked(true);
	}
}

void AStorageChest::SetLocked(bool bInLocked)
{
	if (!HasAuthorityOrIsTesting() || bLocked == bInLocked)
	{
		return;
	}

	bLocked = bInLocked;
	ApplyLockPresentation();
	ForceNetUpdate();

	if (!bLocked)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(It->Get()))
			{
				PlayerController->CloseStorageFromServer(this);
			}
		}
	}
}

void AStorageChest::HandleInteracted(AActor* Interactor)
{
	const bool bLogInteraction = IsStorageInteractionLoggingEnabled();
	if (bLogInteraction)
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[Chest] Interacted. Chest=%s Interactor=%s Authority=%d Locked=%d Component=%s"),
			*GetNameSafe(this), *GetNameSafe(Interactor), HasAuthority(), bLocked,
			*GetNameSafe(InteractableComponent));
	}
	if (!HasAuthority() || !Interactor || bLocked)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Chest] Rejected: no authority, no interactor, or locked."));
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	if (!Player)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Chest] Rejected: interactor is not ABasePlayer."));
		return;
	}

	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(Player->GetController());
	if (!PlayerController)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Chest] Rejected: player has no ABasePlayerController. Controller=%s"), *GetNameSafe(Player->GetController()));
		return;
	}

	bHasBeenOpened = true;
	if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Chest] Requesting server storage open. Controller=%s"), *GetNameSafe(PlayerController));
	PlayerController->OpenStorageFromServer(this);
}

void AStorageChest::HandleStorageChanged()
{
	if (!HasAuthority() || !bDestroyWhenEmpty || !bHasBeenOpened || !StorageComponent)
	{
		return;
	}

	if (StorageComponent->IsEmpty())
	{
		if (!GetWorldTimerManager().IsTimerActive(EmptyDestroyTimerHandle))
		{
			GetWorldTimerManager().SetTimer(
				EmptyDestroyTimerHandle,
				this,
				&AStorageChest::HandleEmptyDestroyTimeout,
				FMath::Max(0.05f, EmptyDestroyDelay),
				false
			);
		}
	}
	else
	{
		GetWorldTimerManager().ClearTimer(EmptyDestroyTimerHandle);
	}
}

void AStorageChest::HandleEmptyDestroyTimeout()
{
	if (!HasAuthority() || !bDestroyWhenEmpty || !StorageComponent || !StorageComponent->IsEmpty())
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(It->Get()))
		{
			PlayerController->CloseStorageFromServer(this);
		}
	}

	Destroy();
}

void AStorageChest::HandleTrackedHealthDeath(UBaseHealthComponent* HealthComponent)
{
	if (!HasAuthorityOrIsTesting() || !HealthComponent)
	{
		return;
	}

	if (HealthComponent == OwningShipHealthComponent)
	{
		bGuardFailed = true;
		SetLocked(true);

		for (UBaseHealthComponent* GuardHealth : AliveGuardHealthComponents)
		{
			if (GuardHealth)
			{
				GuardHealth->OnDeathStarted.RemoveDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
			}
		}
		AliveGuardHealthComponents.Reset();
		ForceNetUpdate();
		return;
	}

	HealthComponent->OnDeathStarted.RemoveDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
	AliveGuardHealthComponents.Remove(HealthComponent);

	if (!bGuardFailed && AliveGuardHealthComponents.IsEmpty())
	{
		SetLocked(false);
	}
}

void AStorageChest::HandleOwningShipDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthorityOrIsTesting())
	{
		return;
	}

	bGuardFailed = true;
	SetLocked(true);
	ClearGuardBindings();
	ForceNetUpdate();
	Destroy();
}

void AStorageChest::ClearLegacyChestDefinition()
{
	ChestDefinition = nullptr;
	bDefinitionInitialized = false;
}

void AStorageChest::ReplaceProgressionLoot(const TArray<FProgressionComputedDrop>& Drops,
	const UItemData* Definitions, int32 Seed)
{
	if (!HasAuthority() || !StorageComponent || !Definitions) return;
	TArray<FStorageItemEntry> Items;
	for (const FInventorySlot& Slot : StorageComponent->GetSlots())
	{
		if (Slot.IsEmpty()) continue;
		const FItemDefinition* Definition = Definitions->FindItemDefinition(Slot.ItemTag);
		const EItemProgressionKind Kind = Definition ? Definition->ProgressionKind : EItemProgressionKind::None;
		if (Kind == EItemProgressionKind::WeaponMaterial || Kind == EItemProgressionKind::ConsumableMaterial
			|| Kind == EItemProgressionKind::ShipMaterial || Kind == EItemProgressionKind::WeaponSpecialMaterial
			|| Kind == EItemProgressionKind::ConsumableSpecialMaterial || Kind == EItemProgressionKind::ShipSpecialMaterial
			|| Kind == EItemProgressionKind::UniversalSpecialMaterial)
		{
			continue;
		}
		FStorageItemEntry& Preserved = Items.AddDefaulted_GetRef();
		Preserved.ItemTag = Slot.ItemTag;
		Preserved.Count = Slot.Count;
	}
	FRandomStream Stream(Seed);
	for (const FProgressionComputedDrop& Drop : Drops)
	{
		if (!Drop.ItemTag.IsValid() || Stream.FRand() >= Drop.Chance) continue;
		FStorageItemEntry& Rolled = Items.AddDefaulted_GetRef();
		Rolled.ItemTag = Drop.ItemTag;
		Rolled.Count = Stream.RandRange(Drop.MinCount, Drop.MaxCount);
	}
	// A sparse level can have only one active chest. Independent probability
	// rolls must never leave its progression reward completely empty.
	if (Items.IsEmpty())
	{
		const FProgressionComputedDrop* BestDrop = nullptr;
		for (const FProgressionComputedDrop& Drop : Drops)
		{
			if (Drop.ItemTag.IsValid() && Drop.Chance > 0.f
				&& (!BestDrop || Drop.Chance > BestDrop->Chance)) BestDrop = &Drop;
		}
		if (BestDrop)
		{
			FStorageItemEntry& Guaranteed = Items.AddDefaulted_GetRef();
			Guaranteed.ItemTag = BestDrop->ItemTag;
			Guaranteed.Count = FMath::Max(1, BestDrop->MinCount);
		}
	}
	int32 NeededSlots = Items.Num();
	for (const FStorageItemEntry& Item : Items)
	{
		NeededSlots += FMath::Max(0, FMath::DivideAndRoundUp(Item.Count,
			FMath::Max(1, StorageComponent->GetMaxStack(Item.ItemTag))) - 1);
	}
	StorageComponent->ConfigureStorage(FMath::Max(StorageComponent->GetSlotCount(), NeededSlots),
		StorageComponent->GetStorageColumns(), Items);
}

void AStorageChest::AppendFixedLoot(const TArray<FStorageItemEntry>& ExtraItems)
{
	if (!HasAuthority() || !StorageComponent || ExtraItems.IsEmpty()) return;
	TArray<FStorageItemEntry> AllItems;
	for (const FInventorySlot& Slot : StorageComponent->GetSlots())
	{
		if (Slot.IsEmpty()) continue;
		FStorageItemEntry& Existing = AllItems.AddDefaulted_GetRef();
		Existing.ItemTag = Slot.ItemTag;
		Existing.Count = Slot.Count;
	}
	AllItems.Append(ExtraItems);
	int32 NeededSlots = 0;
	for (const FStorageItemEntry& Item : AllItems)
	{
		NeededSlots += FMath::DivideAndRoundUp(Item.Count,
			FMath::Max(1, StorageComponent->GetMaxStack(Item.ItemTag)));
	}
	StorageComponent->ConfigureStorage(FMath::Max(StorageComponent->GetSlotCount(), NeededSlots),
		StorageComponent->GetStorageColumns(), AllItems);
}

void AStorageChest::OnRep_Locked()
{
	ApplyLockPresentation();
}

void AStorageChest::OnRep_ReplicatedMovement()
{
	if (bEnablePhysicsAndBuoyancy)
	{
		ClientMovementTargetLocation = GetReplicatedMovement().Location;
		ClientMovementTargetRotation = GetReplicatedMovement().Rotation.Quaternion();
		ClientMovementTargetVelocity = GetReplicatedMovement().LinearVelocity;
		ClientMovementTargetReceiveTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		bHasClientMovementTarget = true;

		// Do not call the engine implementation for floating chests. It mirrors
		// bRepPhysics by enabling client Chaos simulation, which has gravity but no
		// client buoyancy and therefore fights the incoming server corrections.
		if (ChestMesh && ChestMesh->IsSimulatingPhysics())
		{
			ChestMesh->SetSimulatePhysics(false);
		}
		if (ChestMesh)
		{
			ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		if (bDistanceOptimizationDormant)
		{
			SetActorLocationAndRotation(ClientMovementTargetLocation, ClientMovementTargetRotation,
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		SetActorTickEnabled(!bDistanceOptimizationDormant);
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AStorageChest::OnRep_PhysicsMode()
{
	ApplyPhysicsMode();
}

void AStorageChest::OnRep_DistanceOptimizationDormant()
{
	// Clients only smooth the authoritative transform; they never simulate buoyancy.
	ClientMovementTargetVelocity = FVector::ZeroVector;
	bHasClientMovementTarget = false;
	SetActorTickEnabled(false);
}

void AStorageChest::RefreshDistanceOptimizationTimer()
{
	if (!HasAuthority() || !HasActorBegunPlay() || !GetWorld())
	{
		return;
	}

	const bool bEligible = bEnableDistanceOptimization && bEnablePhysicsAndBuoyancy
		&& !IsValid(OwningShip) && !GetAttachParentActor();
	if (!bEligible)
	{
		GetWorldTimerManager().ClearTimer(DistanceOptimizationTimerHandle);
		DistanceOptimizationStableTime = 0.0f;
		if (bDistanceOptimizationDormant)
		{
			SetDistanceOptimizationDormant(false);
		}
		return;
	}

	if (!GetWorldTimerManager().IsTimerActive(DistanceOptimizationTimerHandle))
	{
		GetWorldTimerManager().SetTimer(DistanceOptimizationTimerHandle, this,
			&AStorageChest::EvaluateDistanceOptimization, 0.5f, true, 0.5f);
	}
}

void AStorageChest::EvaluateDistanceOptimization()
{
	if (!HasAuthority() || !GetWorld() || !ChestMesh || !bEnableDistanceOptimization
		|| !bEnablePhysicsAndBuoyancy || IsValid(OwningShip) || GetAttachParentActor())
	{
		RefreshDistanceOptimizationTimer();
		return;
	}

	const float RangeSquared = FMath::Square(FMath::Max(0.0f, DistanceOptimizationRange));
	const FVector ChestLocation = GetActorLocation();
	bool bPlayerInRange = false;
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		const AShip* Ship = *It;
		if (IsValid(Ship) && !Ship->IsEnemyShipForEffects()
			&& Ship->ActorHasTag(TEXT("Player")) && !Ship->ActorHasTag(TEXT("Enemy"))
			&& FVector::DistSquared2D(ChestLocation, Ship->GetActorLocation()) <= RangeSquared)
		{
			bPlayerInRange = true;
			break;
		}
	}
	// A player can swim away from a distant ship and interact with a chest.
	if (!bPlayerInRange)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (IsValid(Pawn)
				&& FVector::DistSquared2D(ChestLocation, Pawn->GetActorLocation()) <= RangeSquared)
			{
				bPlayerInRange = true;
				break;
			}
		}
	}

	if (bPlayerInRange)
	{
		DistanceOptimizationStableTime = 0.0f;
		SetDistanceOptimizationDormant(false);
		return;
	}
	if (bDistanceOptimizationDormant)
	{
		return;
	}

	// Do not pin a freshly dropped chest in midair or while it is still settling.
	const FSWBuoyancyRuntimeDiagnostic& Diagnostic = SWBuoyancyComponent->GetLastRuntimeDiagnostic();
	const bool bSettledInWater = Diagnostic.bPontoonInWater
		&& ChestMesh->IsSimulatingPhysics()
		&& ChestMesh->GetPhysicsLinearVelocity().SizeSquared() <= FMath::Square(100.0f)
		&& ChestMesh->GetPhysicsAngularVelocityInDegrees().SizeSquared() <= FMath::Square(30.0f);
	DistanceOptimizationStableTime = bSettledInWater
		? DistanceOptimizationStableTime + 0.5f : 0.0f;
	if (DistanceOptimizationStableTime >= 2.0f)
	{
		SetDistanceOptimizationDormant(true);
	}
}

void AStorageChest::SetDistanceOptimizationDormant(bool bDormant)
{
	if (!HasAuthority() || bDistanceOptimizationDormant == bDormant || !ChestMesh)
	{
		return;
	}
	if (bDormant)
	{
		ChestMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ChestMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	bDistanceOptimizationDormant = bDormant;
	ApplyPhysicsMode();
	ForceNetUpdate();
}

void AStorageChest::InitializeGuardState()
{
	if (!HasAuthorityOrIsTesting())
	{
		return;
	}

	ClearGuardBindings();
	bGuardFailed = false;

	if (!bRequiresGuardClear)
	{
		SetLocked(false);
		return;
	}

	SetLocked(true);

	int32 ValidConfiguredGuardCount = 0;
	for (ABaseCharacter* GuardCharacter : GuardCharacters)
	{
		if (!IsValid(GuardCharacter))
		{
			continue;
		}

		UBaseHealthComponent* GuardHealth = GuardCharacter->FindComponentByClass<UBaseHealthComponent>();
		if (!GuardHealth)
		{
			for (UActorComponent* Comp : GuardCharacter->GetInstanceComponents())
			{
				if (UBaseHealthComponent* CastHealth = Cast<UBaseHealthComponent>(Comp))
				{
					GuardHealth = CastHealth;
					break;
				}
			}
		}
		if (!GuardHealth)
		{
			UE_LOG(LogTemp, Error, TEXT("Guarded chest %s: guard %s has no BaseHealthComponent."),
				*GetName(), *GetNameSafe(GuardCharacter));
			continue;
		}

		++ValidConfiguredGuardCount;
		if (!GuardHealth->IsDead())
		{
			AliveGuardHealthComponents.Add(GuardHealth);
			GuardHealth->OnDeathStarted.AddUniqueDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
		}
	}

	if (OwningShip)
	{
		OwningShip->OnDestroyed.AddUniqueDynamic(this, &AStorageChest::HandleOwningShipDestroyed);
		OwningShipHealthComponent = OwningShip->FindComponentByClass<UBaseHealthComponent>();
		if (OwningShipHealthComponent)
		{
			OwningShipHealthComponent->OnDeathStarted.AddUniqueDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
			if (OwningShipHealthComponent->IsDead())
			{
				HandleTrackedHealthDeath(OwningShipHealthComponent);
				return;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Guarded ship chest %s: owning ship %s has no BaseHealthComponent."),
				*GetName(), *GetNameSafe(OwningShip));
		}
	}

	if (ValidConfiguredGuardCount == 0)
	{
		// 보호하는 적이 등록되지 않은 경우, 테스트 및 기본 열림을 위해 잠금을 해제한다.
		SetLocked(false);
		return;
	}

	if (AliveGuardHealthComponents.IsEmpty())
	{
		SetLocked(false);
	}
}

void AStorageChest::ClearGuardBindings()
{
	for (UBaseHealthComponent* GuardHealth : AliveGuardHealthComponents)
	{
		if (GuardHealth)
		{
			GuardHealth->OnDeathStarted.RemoveDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
		}
	}
	AliveGuardHealthComponents.Reset();

	if (OwningShipHealthComponent)
	{
		OwningShipHealthComponent->OnDeathStarted.RemoveDynamic(this, &AStorageChest::HandleTrackedHealthDeath);
		OwningShipHealthComponent = nullptr;
	}

	if (OwningShip)
	{
		OwningShip->OnDestroyed.RemoveDynamic(this, &AStorageChest::HandleOwningShipDestroyed);
	}
}

void AStorageChest::ApplyPhysicsMode()
{
	if (!ChestMesh)
	{
		return;
	}

	SetActorTickEnabled(!HasAuthority() && bEnablePhysicsAndBuoyancy && !bDistanceOptimizationDormant);

	if (bEnablePhysicsAndBuoyancy)
	{
		if (HasAuthority())
		{
			ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ChestMesh->SetMassOverrideInKg(NAME_None, PhysicsMassKg, true);
			if (ChestMesh->IsSimulatingPhysics() == bDistanceOptimizationDormant)
			{
				ChestMesh->SetSimulatePhysics(!bDistanceOptimizationDormant);
				if (!bDistanceOptimizationDormant)
				{
					ChestMesh->WakeAllRigidBodies();
				}
			}
		}
		else
		{
			ChestMesh->SetSimulatePhysics(false);
			ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		if (SWBuoyancyComponent && HasAuthority() && !bDistanceOptimizationDormant)
		{
			SWBuoyancyComponent->Activate();
			SWBuoyancyComponent->SetComponentTickEnabled(true);
		}
		else if (SWBuoyancyComponent)
		{
			SWBuoyancyComponent->Deactivate();
			SWBuoyancyComponent->SetComponentTickEnabled(false);
		}
		return;
	}

	ChestMesh->SetSimulatePhysics(false);
	bHasClientMovementTarget = false;
	if (SWBuoyancyComponent)
	{
		SWBuoyancyComponent->Deactivate();
	}
}

void AStorageChest::ApplyLockPresentation()
{
	if (InteractableComponent)
	{
		InteractableComponent->InitializeInteractable(StorageName, bLocked ? LockedActionText : ActionText);
	}
}
