// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageChest.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "InteractableComponent.h"

AStorageChest::AStorageChest()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	SetRootComponent(ChestMesh);

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

	// The root must support physics for both server rigid-body simulation and built-in physics replication.
	// Keep the Blueprint collision responses, but promote QueryOnly profiles to QueryAndPhysics at runtime.
	if (ChestMesh)
	{
		ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	if (HasAuthority())
	{
		if (ChestMesh)
		{
			ChestMesh->SetMassOverrideInKg(NAME_None, PhysicsMassKg, true);
			ChestMesh->SetSimulatePhysics(true);
			ChestMesh->WakeAllRigidBodies();
		}

	}

	if (InteractableComponent)
	{
		InteractableComponent->InitializeInteractable(StorageName, ActionText);
		InteractableComponent->OnInteracted.AddUniqueDynamic(this, &AStorageChest::HandleInteracted);
	}
}

void AStorageChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("AStorageChest::EndPlay - Chest=%s Reason=%d Owner=%s Location=%s"),
		*GetName(),
		static_cast<int32>(EndPlayReason),
		*GetNameSafe(GetOwner()),
		*GetActorLocation().ToString());

	Super::EndPlay(EndPlayReason);
}

void AStorageChest::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
	if (StorageComponent)
	{
		StorageComponent->ConfigureStorage(InSlotCount, InColumnCount, InItems);
	}
}

void AStorageChest::HandleInteracted(AActor* Interactor)
{
	if (!HasAuthority() || !Interactor)
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
