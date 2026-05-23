#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "WaveSpawnTypes.generated.h"

class ABaseEnemy;

/**
 * Enemy가 사망한 이유.
 *
 * 멀티플레이 기준:
 * - 이 값은 서버의 WaveSpawnManager가 AliveEnemyCount를 줄일 때 사용한다.
 * - 클라이언트 UI 표시용으로 나중에 이벤트를 복제하거나 RPC로 전달할 수 있다.
 */
UENUM(BlueprintType)
enum class EWaveEnemyRemoveReason : uint8
{
    Death       UMETA(DisplayName = "Death"),
    GoalReached UMETA(DisplayName = "Goal Reached"),
    Despawn     UMETA(DisplayName = "Despawn"),
    Unknown     UMETA(DisplayName = "Unknown")
};

/**
 * SpawnGroup의 서버 런타임 상태
 * 서버에서 내부적으로 관리하는 상태
 * 
 * 주의:
 * - 이 값 자체는 DataAsset에 저장되는 데이터가 아니다.
 * - FSpawnGroupRuntime에서 사용되는 서버 실행 상태다.
 */
UENUM(BlueprintType)
enum class EWaveSpawnGroupState : uint8
{
    Waiting   UMETA(DisplayName = "Waiting"),
    Spawning  UMETA(DisplayName = "Spawning"),
    Finished  UMETA(DisplayName = "Finished"),
    Cancelled UMETA(DisplayName = "Cancelled")
};

/**
 * 한 SpawnGroup의 정적 데이터.
 *
 * 예:
 * - Wave 1의 Group 0: Grunt 10마리를 Main Route로 0.5초마다 1마리씩 생성
 * - Wave 2의 Group 1: Runner 12마리를 Left Route로 1초마다 3마리씩 생성
 */
USTRUCT(BlueprintType)
struct FSpawnGroupDefinition
{
    GENERATED_BODY()

public:
    /**
     * 생성할 Enemy 클래스.
     *
     * - Count는 AWaveSpawnManager가 독립 관리해야 한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup")
    TSubclassOf<ABaseEnemy> EnemyClass = nullptr;

    
    // Group에서 총 생성할 수.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup", meta = (ClampMin = "1"))
    int32 Count = 10;

    /**
     * 이 Group이 사용할 RouteId.
     *
     * 이후 AWaveSpawnManager가 레벨에 배치된 ASpawnRoute들을 수집해서
     * RouteId -> ASpawnRoute 맵을 만들고 이 값을 기준으로 Route를 찾는다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup")
    FName RouteId = NAME_None;

    /**
     * Wave 시작 후 이 Group이 스폰을 시작하기까지의 지연 시간.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup", meta = (ClampMin = "0.0", Units = "s"))
    float StartDelay = 0.0f;

    /**
     * Burst 단위 스폰 간격.
     * BurstCount가 3이면 SpawnInterval마다 최대 3마리씩 생성한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup", meta = (ClampMin = "0.0", Units = "s"))
    float SpawnInterval = 0.35f;

    /**
     * 한 번의 Spawn Tick에서 몇 마리를 생성할지.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup", meta = (ClampMin = "1"))
    int32 BurstCount = 1;

    /**
     * Route 시작점 주변 스폰 반경 - 퍼져서 Spawn되도록
     * 실제 적용은 이후 ASpawnRoute::GetSpawnTransform에서 처리한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|SpawnGroup", meta = (ClampMin = "0.0", Units = "cm"))
    float SpawnRadius = 80.0f;

    /**
     * 체력 배율.
     * 실제 적용은 이후 AWaveEnemyBase::InitWaveEnemy에서 처리한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Scaling", meta = (ClampMin = "0.01"))
    float HealthMultiplier = 1.0f;

    /**
     * 이동속도 배율.
     * 실제 적용은 이후 AWaveEnemyBase::InitWaveEnemy에서 처리한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Scaling", meta = (ClampMin = "0.01"))
    float SpeedMultiplier = 1.0f;

    /**
     * Enemy 레벨.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Scaling", meta = (ClampMin = "1"))
    int32 EnemyLevel = 1;
};

/**
 * 한 Wave의 정적 데이터.
 *
 * 내부 실행은 배열 인덱스인 WaveArrayIndex로 한다.
 * UI 표시는 DisplayWaveNumber를 사용한다.
 */
USTRUCT(BlueprintType)
struct FWaveDefinition
{
    GENERATED_BODY()

public:
    /**
     * UI에 보여줄 Wave 번호.
     *
     * 예:
     * - 배열 인덱스 0 -> DisplayWaveNumber 1
     * - 배열 인덱스 1 -> DisplayWaveNumber 2
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "1"))
    int32 DisplayWaveNumber = 1;

    /**
     * 이 Wave 시작 전 대기 시간.
     *
     * 실제 타이머 적용은 GameModeV2 또는 WaveSpawnManager에서 선택적으로 처리한다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0", Units = "s"))
    float PreWaveDelay = 3.0f;

    /**
     * 웨이브 제한 시간
     * 0이하 = 제한 없음
     * 0 초과 = 해당 시간 내에 클리어
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0", Units = "s"))
    float WaveTimeLimit = 0.0f;
    
    /**
     * 이 Wave 안에서 실행할 SpawnGroup들.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
    TArray<FSpawnGroupDefinition> SpawnGroups;

    /**
     * Wave 완료 후 자동으로 다음 Wave를 시작할지 여부.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
    bool bAutoStartNextWave = false;

    /**
     * 자동 시작 시 다음 Wave까지의 지연 시간.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0", Units = "s"))
    float NextWaveDelay = 5.0f;

    
};

/**
 * SpawnGroup 실행 중 서버 런타임 상태.
 *
 * 주의:
 * - DataAsset에 저장하지 않는다.
 * - 클라이언트 복제 대상도 아니다.
 * - AWaveSpawnManager 내부에서만 사용한다.
 */
USTRUCT()
struct FSpawnGroupRuntime
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 RemainingCount = 0;

    UPROPERTY()
    int32 SpawnedCount = 0;

    UPROPERTY()
    bool bFinished = false;

    UPROPERTY()
    EWaveSpawnGroupState State = EWaveSpawnGroupState::Waiting;

    /**
     * FTimerHandle은 UPROPERTY로 둘 필요가 없다.
     * 서버 런타임에서만 쓰고 저장/복제하지 않는다.
     */
    FTimerHandle TimerHandle;

public:
    void Reset()
    {
        RemainingCount = 0;
        SpawnedCount = 0;
        bFinished = false;
        State = EWaveSpawnGroupState::Waiting;
        TimerHandle.Invalidate();
    }

    void InitializeFromDefinition(const FSpawnGroupDefinition& Definition)
    {
        RemainingCount = FMath::Max(0, Definition.Count);
        SpawnedCount = 0;
        bFinished = RemainingCount <= 0;
        State = bFinished ? EWaveSpawnGroupState::Finished : EWaveSpawnGroupState::Waiting;
        TimerHandle.Invalidate();
    }
};