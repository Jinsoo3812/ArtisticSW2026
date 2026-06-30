// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageChest.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"

AStorageChest::AStorageChest()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	ChestMesh->SetupAttachment(RootComponent);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(RootComponent);

	StorageComponent = CreateDefaultSubobject<UStorageComponent>(TEXT("StorageComponent"));
}

void AStorageChest::BeginPlay()
{
	Super::BeginPlay();

	if (InteractableComponent)
	{
		InteractableComponent->InitializeInteractable(StorageName, ActionText);
		InteractableComponent->OnInteracted.AddUniqueDynamic(this, &AStorageChest::HandleInteracted);
	}
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
