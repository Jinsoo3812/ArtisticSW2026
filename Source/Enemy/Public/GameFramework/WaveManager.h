// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaveManager.generated.h"

class ABaseEnemy;
class AEnemySpawnPoint;
class ASWWaveGameMode;

// 한 스폰 포인트에서 나올 적 그룹 정보
USTRUCT(BlueprintType)
struct FWaveGroupInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave Data")
	TSubclassOf<ABaseEnemy> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave Data", meta = (ClampMin = "1"))
	int32 SpawnCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave Data", meta = (ClampMin = "0.1"))
	float SpawnInterval = 1.0f;

	// 적이 생성되어 출발할 스폰 포인트 (맵에 배치된 액터를 스포이드로 지정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave Data")
	AEnemySpawnPoint* SpawnPoint = nullptr;
};

// 하나의 웨이브(예: Wave 1)에 포함된 모든 스폰 정보
USTRUCT(BlueprintType)
struct FWaveDef
{
	GENERATED_BODY()

	// 이 배열에 여러 그룹을 넣으면, 다방향 동시 스폰(예: 북쪽 문 오크 5마리, 남쪽 문 고블린 10마리)이 가능합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave Data")
	TArray<FWaveGroupInfo> WaveGroups;
};

// 스폰을 예약하기 위한 내부용 태스크 구조체
struct FSpawnTask
{
	float TimeToSpawn;
	TSubclassOf<ABaseEnemy> EnemyClass;
	AEnemySpawnPoint* SpawnPoint;

	// 시간순 정렬을 위한 연산자 오버로딩
	bool operator<(const FSpawnTask& Other) const
	{
		return TimeToSpawn < Other.TimeToSpawn;
	}
};



UCLASS()
class ENEMY_API AWaveManager : public AActor
{
	GENERATED_BODY()

public:
	AWaveManager();

protected:
	virtual void BeginPlay() override;

	// 웨이브 설정 데이터 (인덱스 0이 Wave 1이 됩니다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave Config")
	TArray<FWaveDef> WaveConfigs;

	// 현재 레벨의 게임모드 캐싱
	UPROPERTY()
	ASWWaveGameMode* WaveGameMode;

private:
	// 스폰 대기열
	TArray<FSpawnTask> SpawnQueue;
	FTimerHandle SpawnTimerHandle;

	// GameMode의 OnWaveStarted 델리게이트에 바인딩될 함수
	UFUNCTION()
	void HandleWaveStarted(int32 WaveIndex);

	// 큐에서 꺼내어 실제로 적을 생성하는 함수
	void ProcessNextSpawn();
};