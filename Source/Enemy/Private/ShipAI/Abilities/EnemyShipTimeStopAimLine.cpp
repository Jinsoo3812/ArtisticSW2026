#include "ShipAI/Abilities/EnemyShipTimeStopAimLine.h"

#include "Cannon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "UObject/ConstructorHelpers.h"

AEnemyShipTimeStopAimLine::AEnemyShipTimeStopAimLine()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(10.0f);
	SetReplicateMovement(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	LineMesh->SetupAttachment(SceneRoot);
	LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LineMesh->SetGenerateOverlapEvents(false);
	LineMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		LineMesh->SetStaticMesh(CylinderFinder.Object);
	}
	// Engine Cylinder is Z-aligned. Rotate it so its length follows this actor's X axis.
	LineMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
}

void AEnemyShipTimeStopAimLine::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AEnemyShipTimeStopAimLine::InitializeAimLine(
	const FVector& InStart,
	const FVector& InDirection,
	AShip* InTargetShip,
	float InMaximumDistance,
	float InTraceIntervalSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	LineStart = InStart;
	SourceCannon.Reset();
	FixedTargetPoint = FVector::ZeroVector;
	FixedDirection = InDirection.GetSafeNormal();
	TargetShip = InTargetShip;
	MaximumDistance = FMath::Max(1.0f, InMaximumDistance);
	TraceIntervalSeconds = FMath::Max(0.01f, InTraceIntervalSeconds);
	TraceTimeAccumulator = 0.0f;
	UpdateClippedEndpoint();
}

void AEnemyShipTimeStopAimLine::InitializeAimLineFromCannon(
	ACannon* InSourceCannon,
	const FVector& InFixedTargetPoint,
	AShip* InTargetShip,
	float InMaximumDistance,
	float InTraceIntervalSeconds)
{
	if (!HasAuthority() || !InSourceCannon)
	{
		return;
	}
	SourceCannon = InSourceCannon;
	FixedTargetPoint = InFixedTargetPoint;
	LineStart = InSourceCannon->GetProjectileMuzzleTransform().GetLocation();
	FixedDirection = (FixedTargetPoint - FVector(LineStart)).GetSafeNormal();
	TargetShip = InTargetShip;
	MaximumDistance = FMath::Max(1.0f, InMaximumDistance);
	TraceIntervalSeconds = FMath::Max(0.01f, InTraceIntervalSeconds);
	TraceTimeAccumulator = 0.0f;
	UpdateClippedEndpoint();
}

void AEnemyShipTimeStopAimLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	TraceTimeAccumulator += DeltaSeconds;
	if (TraceTimeAccumulator >= TraceIntervalSeconds)
	{
		TraceTimeAccumulator = FMath::Fmod(TraceTimeAccumulator, TraceIntervalSeconds);
		UpdateClippedEndpoint();
	}
}

FVector AEnemyShipTimeStopAimLine::ResolveClippedLineEnd(
	const FVector& InStart,
	const FVector& InDirection,
	const AShip* InTargetShip,
	float InMaximumDistance)
{
	const FVector Direction = InDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return InStart;
	}
	const FVector FarEnd = InStart + Direction * FMath::Max(1.0f, InMaximumDistance);
	FHitResult Hit;
	if (InTargetShip && InTargetShip->ShipDamageMesh
		&& InTargetShip->ShipDamageMesh->LineTraceComponent(
			Hit, InStart, FarEnd, FCollisionQueryParams()))
	{
		return Hit.ImpactPoint;
	}
	return FarEnd;
}

void AEnemyShipTimeStopAimLine::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, LineStart);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, LineEnd);
}

void AEnemyShipTimeStopAimLine::OnRep_LineEndpoints()
{
	RefreshLineVisual();
}

void AEnemyShipTimeStopAimLine::UpdateClippedEndpoint()
{
	bool bEndpointsChanged = false;
	if (ACannon* Cannon = SourceCannon.Get())
	{
		const FVector NewStart = Cannon->GetProjectileMuzzleTransform().GetLocation();
		const FVector NewDirection = (FixedTargetPoint - NewStart).GetSafeNormal();
		if (!FVector(LineStart).Equals(NewStart, 0.5f))
		{
			LineStart = NewStart;
			bEndpointsChanged = true;
		}
		if (!NewDirection.IsNearlyZero())
		{
			FixedDirection = NewDirection;
		}
	}
	const FVector NewEnd = ResolveClippedLineEnd(
		FVector(LineStart), FixedDirection, TargetShip.Get(), MaximumDistance);
	if (!FVector(LineEnd).Equals(NewEnd, 0.5f))
	{
		LineEnd = NewEnd;
		bEndpointsChanged = true;
	}
	if (bEndpointsChanged)
	{
		RefreshLineVisual();
		ForceNetUpdate();
	}
}

void AEnemyShipTimeStopAimLine::RefreshLineVisual()
{
	if (!LineMesh)
	{
		return;
	}
	const FVector Delta = FVector(LineEnd) - FVector(LineStart);
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		LineMesh->SetVisibility(false);
		return;
	}

	LineMesh->SetVisibility(true);
	if (LaserMaterial && LineMesh->GetMaterial(0) != LaserMaterial)
	{
		LineMesh->SetMaterial(0, LaserMaterial);
	}
	SetActorLocationAndRotation(
		(FVector(LineStart) + FVector(LineEnd)) * 0.5f,
		Delta.Rotation());
	LineMesh->SetRelativeScale3D(FVector(
		FMath::Max(1.0f, LineThickness) / 100.0f,
		FMath::Max(1.0f, LineThickness) / 100.0f,
		Length / 100.0f));
}
