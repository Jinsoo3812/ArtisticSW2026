// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageChest.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "BuoyancyComponent.h"
#include "BuoyancyTypes.h"
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

	BuoyancyComponent = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("BuoyancyComponent"));
	BuoyancyComponent->SetCanBeActive(false);
	// UE 5.7's async buoyancy snapshot only recognizes a direct UGerstnerWaterWaves
	// object. KKH_Test uses SWRippleWaterWaves as a wrapper, so use the normal
	// game-thread query path that evaluates the wrapper's virtual wave function.
	BuoyancyComponent->bUseAsyncPath = false;
	BuoyancyComponent->BuoyancyData.Pontoons.Reset();
	FSphericalPontoon CenterPontoon;
	CenterPontoon.RelativeLocation = FVector::ZeroVector;
	CenterPontoon.Radius = 50.0f;
	BuoyancyComponent->BuoyancyData.Pontoons.Add(CenterPontoon);

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

		if (BuoyancyComponent)
		{
			BuoyancyComponent->SetCanBeActive(true);
		}
	}
	else if (BuoyancyComponent)
	{
		// Clients receive the server root rigid-body state through replicated movement.
		// They must not submit their own buoyancy forces.
		BuoyancyComponent->SetCanBeActive(false);
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
