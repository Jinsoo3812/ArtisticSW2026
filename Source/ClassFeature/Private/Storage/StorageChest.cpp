// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageChest.h"
#include "BaseCharacter.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "InteractableComponent.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "EngineUtils.h"

AStorageChest::AStorageChest()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
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

	StorageComponent = CreateDefaultSubobject<UStorageComponent>(TEXT("StorageComponent"));
}

void AStorageChest::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && ChestDefinition && !bDefinitionInitialized)
	{
		InitializeFromChestDefinition(ChestDefinition, LootSeed);
	}

	ApplyPhysicsMode();

	if (InteractableComponent)
	{
		InteractableComponent->OnInteracted.AddUniqueDynamic(this, &AStorageChest::HandleInteracted);
	}

	if (HasAuthority())
	{
		InitializeGuardState();
	}

	ApplyLockPresentation();
}

void AStorageChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearGuardBindings();

	Super::EndPlay(EndPlayReason);
}

void AStorageChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStorageChest, bLocked);
	DOREPLIFETIME(AStorageChest, bGuardFailed);
	DOREPLIFETIME(AStorageChest, bEnablePhysicsAndBuoyancy);
}

void AStorageChest::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
	if (StorageComponent)
	{
		StorageComponent->ConfigureStorage(InSlotCount, InColumnCount, InItems);
	}
}

void AStorageChest::InitializeFromChestDefinition(UChestDefinition* InDefinition, int32 Seed)
{
	if (!HasAuthority() || !InDefinition)
	{
		return;
	}

	ChestDefinition = InDefinition;
	LootSeed = Seed;
	bEnablePhysicsAndBuoyancy = InDefinition->bEnablePhysicsAndBuoyancy;
	ConfigureStorage(
		FMath::Max(1, InDefinition->SlotCount),
		FMath::Max(1, InDefinition->ColumnCount),
		InDefinition->RollInitialItems(Seed));
	bDefinitionInitialized = true;

	if (HasActorBegunPlay())
	{
		ApplyPhysicsMode();
	}
}

void AStorageChest::ConfigureGuarding(
	bool bInRequiresGuardClear,
	const TArray<ABaseCharacter*>& InGuardCharacters,
	AShip* InOwningShip)
{
	if (!HasAuthority())
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
	}

	// Configure immediately as well as in BeginPlay. Deferred spawns therefore
	// enter the world already locked, and placed/runtime reconfiguration is deterministic.
	InitializeGuardState();

	if (HasActorBegunPlay())
	{
		ApplyPhysicsMode();
	}
}

void AStorageChest::SetLocked(bool bInLocked)
{
	if (!HasAuthority() || bLocked == bInLocked)
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

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(It->Get()))
		{
			PlayerController->CloseStorageFromServer(this);
		}
	}
}

void AStorageChest::HandleInteracted(AActor* Interactor)
{
	if (!HasAuthority() || !Interactor || bLocked)
	{
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	if (!Player)
	{
		return;
	}

	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(Player->GetController());
	if (!PlayerController)
	{
		return;
	}

	PlayerController->OpenStorageFromServer(this);
}

void AStorageChest::HandleTrackedHealthDeath(UBaseHealthComponent* HealthComponent)
{
	if (!HasAuthority() || !HealthComponent)
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
	if (HasAuthority() && DestroyedActor == OwningShip)
	{
		Destroy();
	}
}

void AStorageChest::OnRep_Locked()
{
	ApplyLockPresentation();
}

void AStorageChest::OnRep_PhysicsMode()
{
	ApplyPhysicsMode();
}

void AStorageChest::InitializeGuardState()
{
	if (!HasAuthority())
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
		UE_LOG(LogTemp, Error, TEXT("Guarded chest %s has no valid guards and will remain locked."), *GetName());
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

	if (bEnablePhysicsAndBuoyancy)
	{
		ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (HasAuthority())
		{
			ChestMesh->SetMassOverrideInKg(NAME_None, PhysicsMassKg, true);
			ChestMesh->SetSimulatePhysics(true);
			ChestMesh->WakeAllRigidBodies();
		}
		if (SWBuoyancyComponent)
		{
			SWBuoyancyComponent->Activate();
		}
		return;
	}

	ChestMesh->SetSimulatePhysics(false);
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
