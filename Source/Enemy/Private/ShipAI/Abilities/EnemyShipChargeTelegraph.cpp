#include "ShipAI/Abilities/EnemyShipChargeTelegraph.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AEnemyShipChargeTelegraph::AEnemyShipChargeTelegraph()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(10.0f);
	SetReplicateMovement(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WarningPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WarningPlane"));
	WarningPlane->SetupAttachment(SceneRoot);
	WarningPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarningPlane->SetGenerateOverlapEvents(false);
	WarningPlane->SetCastShadow(false);
	WarningPlane->SetReceivesDecals(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		WarningPlane->SetStaticMesh(PlaneFinder.Object);
	}
}

void AEnemyShipChargeTelegraph::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisual();
}

void AEnemyShipChargeTelegraph::InitializeTelegraph(
	const FVector& InStart,
	const FVector& InDirection,
	float InDistance,
	float InWidth,
	float InWorldZ)
{
	if (!HasAuthority())
	{
		return;
	}

	TelegraphWidth = FMath::Max(1.0f, InWidth);
	FVector Start = InStart;
	Start.Z = InWorldZ;
	TelegraphStart = Start;
	const FVector Direction = InDirection.GetSafeNormal2D();
	TelegraphEnd = Start + Direction * FMath::Max(1.0f, InDistance);
	RefreshVisual();
	ForceNetUpdate();
}

void AEnemyShipChargeTelegraph::UpdateTelegraph(
	const FVector& InStart,
	const FVector& InDirection)
{
	if (!HasAuthority())
	{
		return;
	}

	const float Distance = FVector::Dist2D(TelegraphStart, TelegraphEnd);
	FVector Start = InStart;
	Start.Z = TelegraphStart.Z;
	const FVector Direction = InDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	TelegraphStart = Start;
	TelegraphEnd = Start + Direction * FMath::Max(1.0f, Distance);
	RefreshVisual();
	ForceNetUpdate();
}

void AEnemyShipChargeTelegraph::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipChargeTelegraph, TelegraphStart);
	DOREPLIFETIME(AEnemyShipChargeTelegraph, TelegraphEnd);
	DOREPLIFETIME(AEnemyShipChargeTelegraph, TelegraphWidth);
}

void AEnemyShipChargeTelegraph::OnRep_TelegraphGeometry()
{
	RefreshVisual();
}

void AEnemyShipChargeTelegraph::RefreshVisual()
{
	if (!WarningPlane)
	{
		return;
	}

	const FVector Delta = FVector(TelegraphEnd) - FVector(TelegraphStart);
	const float Length = Delta.Size2D();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		WarningPlane->SetVisibility(false);
		return;
	}

	WarningPlane->SetVisibility(true);
	if (WarningMaterial && !DynamicWarningMaterial)
	{
		DynamicWarningMaterial = UMaterialInstanceDynamic::Create(WarningMaterial, this);
		WarningPlane->SetMaterial(0, DynamicWarningMaterial);
	}
	if (DynamicWarningMaterial)
	{
		DynamicWarningMaterial->SetScalarParameterValue(
			TEXT("ArrowRepeatCount"),
			FMath::Max(1.0f, Length / FMath::Max(1.0f, SymbolSpacing)));
	}

	SetActorLocationAndRotation(
		(FVector(TelegraphStart) + FVector(TelegraphEnd)) * 0.5f,
		FRotator(0.0f, Delta.Rotation().Yaw, 0.0f));
	// Engine Plane is 100 cm square in local XY.
	WarningPlane->SetRelativeScale3D(FVector(
		Length / 100.0f,
		FMath::Max(1.0f, TelegraphWidth) / 100.0f,
		1.0f));
}
