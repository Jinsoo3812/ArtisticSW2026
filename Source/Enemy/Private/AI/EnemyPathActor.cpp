// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/EnemyPathActor.h"

// Unreal
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"

AEnemyPathActor::AEnemyPathActor()
{
	//PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	SetReplicateMovement(false);
	
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PathSpline = CreateDefaultSubobject<USplineComponent>(TEXT("PathSpline"));
	PathSpline->SetupAttachment(Root);

	// 맵에서 바로 편집하기 편하도록 기본 설정
	PathSpline->SetClosedLoop(false);
	PathSpline->bDrawDebug = true;
}

void AEnemyPathActor::BeginPlay()
{
	Super::BeginPlay();
}

/*void AEnemyPathActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}*/

float AEnemyPathActor::GetPathLength() const
{
	if (!PathSpline)
	{
		return 0.0f;
	}

	return PathSpline->GetSplineLength();
}

float AEnemyPathActor::ClampDistanceToPath(float Distance) const
{
	const float PathLength = GetPathLength();
	return FMath::Clamp(Distance, 0.0f, PathLength);
}

bool AEnemyPathActor::IsValidDistance(float Distance) const
{
	const float PathLength = GetPathLength();
	return Distance >= 0.0f && Distance <= PathLength;
}

FVector AEnemyPathActor::GetWorldLocationAtDistance(float Distance) const
{
	if (!PathSpline)
	{
		return GetActorLocation();
	}

	const float ClampedDistance = ClampDistanceToPath(Distance);
	return PathSpline->GetLocationAtDistanceAlongSpline(
		ClampedDistance,
		ESplineCoordinateSpace::World
	);
}

FRotator AEnemyPathActor::GetWorldRotationAtDistance(float Distance) const
{
	if (!PathSpline)
	{
		return GetActorRotation();
	}

	const float ClampedDistance = ClampDistanceToPath(Distance);
	return PathSpline->GetRotationAtDistanceAlongSpline(
		ClampedDistance,
		ESplineCoordinateSpace::World
	);
}

FTransform AEnemyPathActor::GetWorldTransformAtDistance(float Distance) const
{
	if (!PathSpline)
	{
		return GetActorTransform();
	}

	const float ClampedDistance = ClampDistanceToPath(Distance);

	const FVector WorldLocation = PathSpline->GetLocationAtDistanceAlongSpline(
		ClampedDistance,
		ESplineCoordinateSpace::World
	);

	const FRotator WorldRotation = PathSpline->GetRotationAtDistanceAlongSpline(
		ClampedDistance,
		ESplineCoordinateSpace::World
	);

	return FTransform(WorldRotation, WorldLocation);
}

FTransform AEnemyPathActor::GetStartTransform() const
{
	return GetWorldTransformAtDistance(0.0f);
}

FTransform AEnemyPathActor::GetEndTransform() const
{
	return GetWorldTransformAtDistance(GetPathLength());
}

