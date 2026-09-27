#include "ShipBoardingPoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"
#include "BoxInteractableComponent.h"
#include "CapsuleInteractableComponent.h"
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

	BoardingBoxInteractable = CreateDefaultSubobject<UBoxInteractableComponent>(TEXT("BoardingBoxInteractable"));
	BoardingBoxInteractable->SetupAttachment(SceneRoot);
	BoardingBoxInteractable->SetCollisionProfileName(TEXT("Interactable"));
	BoardingBoxInteractable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardingBoxInteractable->SetVisibility(false);

	BoardingCapsuleInteractable = CreateDefaultSubobject<UCapsuleInteractableComponent>(TEXT("BoardingCapsuleInteractable"));
	BoardingCapsuleInteractable->SetupAttachment(SceneRoot);
	BoardingCapsuleInteractable->SetCollisionProfileName(TEXT("Interactable"));
	BoardingCapsuleInteractable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardingCapsuleInteractable->SetVisibility(false);

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

	if (bUseLegacySphereAuthoring && InteractionShape == EBoardingInteractionShape::Sphere)
	{
		if (BoardingInteractable)
		{
			BoardingInteractable->SetSphereRadius(FMath::Max(1.0f, InteractionSphereRadius));
			BoardingInteractable->SetRelativeTransform(InteractionRelativeTransform);
		}
	}

	RefreshInteractionShapeState();
}

void AShipBoardingPoint::BeginPlay()
{
	Super::BeginPlay();

	InitializeInteractionShapeComponents();
	RefreshInteractionShapeState();
}

void AShipBoardingPoint::InitializeInteractionShapeComponents()
{
	const FText ObjectText = NSLOCTEXT("ShipInteraction", "BoardingPointObject", "Ship");
	const FText ActionText = NSLOCTEXT("ShipInteraction", "BoardingPointAction", "Board");

	if (BoardingInteractable)
	{
		BoardingInteractable->InitializeInteractable(ObjectText, ActionText);
		BoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShipBoardingPoint::HandleInteracted);
	}

	if (BoardingBoxInteractable)
	{
		BoardingBoxInteractable->InitializeInteractable(ObjectText, ActionText);
		BoardingBoxInteractable->OnInteracted.AddUniqueDynamic(this, &AShipBoardingPoint::HandleInteracted);
	}

	if (BoardingCapsuleInteractable)
	{
		BoardingCapsuleInteractable->InitializeInteractable(ObjectText, ActionText);
		BoardingCapsuleInteractable->OnInteracted.AddUniqueDynamic(this, &AShipBoardingPoint::HandleInteracted);
	}
}

void AShipBoardingPoint::RefreshInteractionShapeState()
{
	const AShip* OwningShip = GetOwningShip();
	const bool bAllowsBoarding = !OwningShip || OwningShip->AllowsPlayerBoarding();

	auto UpdateShape = [bAllowsBoarding](UShapeComponent* ShapeComp, bool bSelected)
	{
		if (!ShapeComp)
		{
			return;
		}

		const bool bEnableQuery = bSelected && bAllowsBoarding;
		ShapeComp->SetCollisionEnabled(bEnableQuery ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		ShapeComp->SetVisibility(bSelected);
		ShapeComp->SetHiddenInGame(true);
	};

	UpdateShape(BoardingInteractable, InteractionShape == EBoardingInteractionShape::Sphere);
	UpdateShape(BoardingBoxInteractable, InteractionShape == EBoardingInteractionShape::Box);
	UpdateShape(BoardingCapsuleInteractable, InteractionShape == EBoardingInteractionShape::Capsule);
}

UShapeComponent* AShipBoardingPoint::GetActiveBoardingInteractable() const
{
	UShapeComponent* SelectedShape = nullptr;
	switch (InteractionShape)
	{
	case EBoardingInteractionShape::Sphere:
		SelectedShape = BoardingInteractable;
		break;
	case EBoardingInteractionShape::Box:
		SelectedShape = BoardingBoxInteractable;
		break;
	case EBoardingInteractionShape::Capsule:
		SelectedShape = BoardingCapsuleInteractable;
		break;
	default:
		SelectedShape = BoardingInteractable;
		break;
	}

	if (!SelectedShape)
	{
		SelectedShape = BoardingInteractable;
	}

	return SelectedShape;
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
