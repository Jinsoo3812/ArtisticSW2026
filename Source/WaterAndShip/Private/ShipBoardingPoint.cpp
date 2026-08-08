#include "ShipBoardingPoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"
#include "Ship.h"

AShipBoardingPoint::AShipBoardingPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	PointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PointMesh"));
	PointMesh->SetupAttachment(SceneRoot);
	PointMesh->SetCollisionProfileName(TEXT("NoCollision"));

	BoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(TEXT("BoardingInteractable"));
	BoardingInteractable->SetupAttachment(SceneRoot);
	BoardingInteractable->SetCollisionProfileName(TEXT("Interactable"));

	bReplicates = true;
	SetReplicateMovement(false);
}

void AShipBoardingPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (PointMesh)
	{
		PointMesh->SetStaticMesh(PointMeshAsset);
		PointMesh->SetRelativeTransform(MeshRelativeTransform);
	}

	if (BoardingInteractable)
	{
		BoardingInteractable->SetSphereRadius(FMath::Max(1.0f, InteractionSphereRadius));
		BoardingInteractable->SetRelativeTransform(InteractionRelativeTransform);
	}
}

void AShipBoardingPoint::BeginPlay()
{
	Super::BeginPlay();

	if (BoardingInteractable)
	{
		BoardingInteractable->InitializeInteractable(
			NSLOCTEXT("ShipInteraction", "BoardingPointObject", "Ship"),
			NSLOCTEXT("ShipInteraction", "BoardingPointAction", "Board"));
		BoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShipBoardingPoint::HandleInteracted);
	}
}

AShip* AShipBoardingPoint::GetOwningShip() const
{
	const AActor* Current = GetParentActor();
	if (!Current)
	{
		Current = GetAttachParentActor();
	}
	if (!Current)
	{
		Current = GetOwner();
	}

	while (Current)
	{
		if (AShip* Ship = const_cast<AShip*>(Cast<AShip>(Current)))
		{
			return Ship;
		}
		Current = Current->GetAttachParentActor();
	}

	return nullptr;
}

void AShipBoardingPoint::HandleInteracted(AActor* Interactor)
{
	if (AShip* Ship = GetOwningShip())
	{
		Ship->BoardFromSea(Interactor);
	}
}
