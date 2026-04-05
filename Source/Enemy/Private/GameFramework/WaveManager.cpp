// Fill out your copyright notice in the Description page of Project Settings.


#include "GameFramework/WaveManager.h"
#include "AI/EnemySpawnPoint.h"
#include "BaseEnemy.h"

// Core
#include "GameFramework/SWWaveGameMode.h"
// Unreal
#include "Engine/World.h"
#include "TimerManager.h"

// Sets default values
AWaveManager::AWaveManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AWaveManager::BeginPlay()
{
	Super::BeginPlay();
	
	// GameMode 캐싱 및 델리게이트 바인딩
	WaveGameMode = Cast<ASWWaveGameMode>(GetWorld()->GetAuthGameMode());
	if (WaveGameMode)
	{
		WaveGameMode->OnWaveStarted.AddDynamic(this, &AWaveManager::HandleWaveStarted);
	}
}

void AWaveManager::HandleWaveStarted(int32 WaveIndex)
{
	// 인덱스 보정 (GameMode의 Wave는 1부터 시작하므로 배열 인덱스에 맞게 -1)
	int32 ArrayIndex = WaveIndex - 1;

	// 웨이브 데이터가 없거나 범위를 벗어나면 스폰 불가
	if (!WaveConfigs.IsValidIndex(ArrayIndex))
	{
		return;
	}

	SpawnQueue.Empty();

	const FWaveDef& CurrentWaveDef = WaveConfigs[ArrayIndex];

	// 웨이브 정보로부터 스폰 태스크를 생성하여 큐에 담기
	for (const FWaveGroupInfo& Group : CurrentWaveDef.WaveGroups)
	{
		if (!Group.EnemyClass || !Group.SpawnPoint) continue;

		for (int32 i = 0; i < Group.SpawnCount; ++i)
		{
			FSpawnTask Task;
			Task.EnemyClass = Group.EnemyClass;
			Task.SpawnPoint = Group.SpawnPoint;
			// ex: 0초, 1초, 2초... 에 스폰되도록 시간 계산
			Task.TimeToSpawn = i * Group.SpawnInterval; 
			
			SpawnQueue.Add(Task);
		}
	}

	// 일찍 스폰되어야 할 적부터 나오도록 시간 오름차순 정렬
	SpawnQueue.Sort();

	// 스폰 큐 프로세스 시작
	if (SpawnQueue.Num() > 0)
	{
		ProcessNextSpawn();
	}
}

void AWaveManager::ProcessNextSpawn()
{
	if (SpawnQueue.Num() == 0) return;

	// 큐의 첫 번째 태스크 가져오기
	FSpawnTask CurrentTask = SpawnQueue[0];
	SpawnQueue.RemoveAt(0);

	// 적 생성 (SpawnPoint의 위치와 회전값 사용)
	FTransform SpawnTransform = CurrentTask.SpawnPoint->GetSpawnTransform();
	ABaseEnemy* SpawnedEnemy = GetWorld()->SpawnActor<ABaseEnemy>(CurrentTask.EnemyClass, SpawnTransform);

	if (SpawnedEnemy)
	{
		// 1. PathMovement 초기화 및 자동 출발
		SpawnedEnemy->InitializePathMovementFromSpawnPoint(CurrentTask.SpawnPoint, true);

		// 2. GameMode에 적 생성 알림 (++AliveEnemyCount)
		if (WaveGameMode)
		{
			WaveGameMode->NotifyEnemySpawned();
		}
	}

	// 다음 스폰 예약
	if (SpawnQueue.Num() > 0)
	{
		FSpawnTask NextTask = SpawnQueue[0];
		float TimeUntilNextSpawn = NextTask.TimeToSpawn - CurrentTask.TimeToSpawn;

		// 너무 짧은 시간(동시 스폰)은 0.01초로 보정하여 엔진 틱 충돌 방지
		TimeUntilNextSpawn = FMath::Max(TimeUntilNextSpawn, 0.01f);

		GetWorldTimerManager().SetTimer(
			SpawnTimerHandle,
			this,
			&AWaveManager::ProcessNextSpawn,
			TimeUntilNextSpawn,
			false
		);
	}
}

