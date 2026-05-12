// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/EnemySpawnPoint.h"
#include "AI/EnemyPathActor.h"

// Unreal
#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/Engine.h"

AEnemySpawnPoint::AEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowSize = 1.5f;
	Arrow->bIsScreenSizeScaled = true;
}

void AEnemySpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	// 시작 시 AssignedPath가 유효하다면 StartDistanceAlongPath를 보정
	if (AssignedPath)
	{
		StartDistanceAlongPath = AssignedPath->ClampDistanceToPath(StartDistanceAlongPath);
	}
}

float AEnemySpawnPoint::GetClampedStartDistance() const
{
	if (!AssignedPath)
	{
		return 0.0f;
	}

	return AssignedPath->ClampDistanceToPath(StartDistanceAlongPath);
}

FTransform AEnemySpawnPoint::GetSpawnTransform() const
{
	// 할당된 Path가 없다면 본인의 Transform 반환
	if (!AssignedPath)
	{
		return GetActorTransform();
	}

	const float ClampedDistance = GetClampedStartDistance();
	return AssignedPath->GetWorldTransformAtDistance(ClampedDistance);
}

// World에 SpawnPoint를 배치했을 때, Path와 해당 Path위의 시작 위치를 넣어주면 해당 지점에 고정
// #if WITH_EDITOR로 감싼 이유는 에디터에 필요한 로직이라 빌드에서 제외하기 위해
#if WITH_EDITOR
void AEnemySpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (AssignedPath)
	{
		StartDistanceAlongPath = AssignedPath->ClampDistanceToPath(StartDistanceAlongPath);

		const FTransform SpawnTransform = AssignedPath->GetWorldTransformAtDistance(StartDistanceAlongPath);
		SetActorLocationAndRotation(
			SpawnTransform.GetLocation(),
			SpawnTransform.GetRotation()
		);
	}
}
#endif