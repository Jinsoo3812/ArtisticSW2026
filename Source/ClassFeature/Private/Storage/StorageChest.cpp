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

void AStorageChest::SetPhysicsAndBuoyancyEnabled(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	bEnablePhysicsAndBuoyancy = bEnabled;
	if (HasActorBegunPlay())
	{
		ApplyPhysicsMode();
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

	bHasBeenOpened = true;
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
	if (!HasAuthority() || !StorageComponent || !StorageComponent->IsEmpty())
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
		SetActorTickEnabled(true);
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AStorageChest::OnRep_PhysicsMode()
{
	ApplyPhysicsMode();
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

	SetActorTickEnabled(!HasAuthority() && bEnablePhysicsAndBuoyancy);

	if (bEnablePhysicsAndBuoyancy)
	{
		if (HasAuthority())
		{
			ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ChestMesh->SetMassOverrideInKg(NAME_None, PhysicsMassKg, true);
			ChestMesh->SetSimulatePhysics(true);
			ChestMesh->WakeAllRigidBodies();
		}
		else
		{
			ChestMesh->SetSimulatePhysics(false);
			ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		if (SWBuoyancyComponent && HasAuthority())
		{
			SWBuoyancyComponent->Activate();
		}
		else if (SWBuoyancyComponent)
		{
			SWBuoyancyComponent->Deactivate();
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
