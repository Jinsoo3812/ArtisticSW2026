#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SWGamePhaseTypes.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "Room/SWRoomStateAdapter.h"
#include "Room/SWVoyageResetParticipant.h"
#include "WaveSpawnManager.generated.h"

USTRUCT()
struct FSWRoomWaveGroupState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) int32 RemainingCount = 0;
	UPROPERTY(SaveGame) int32 SpawnedCount = 0;
	UPROPERTY(SaveGame) bool bFinished = false;
	UPROPERTY(SaveGame) EWaveSpawnGroupState State = EWaveSpawnGroupState::Waiting;
	UPROPERTY(SaveGame) bool bTimerPending = false;
	UPROPERTY(SaveGame) float TimerRemaining = 0.f;
	UPROPERTY(SaveGame) TArray<FSWRoomPendingSpawnTicket> PendingTickets;
};

USTRUCT()
struct FSWRoomWaveState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath WaveDataPath;
	UPROPERTY(SaveGame) int32 WaveArrayIndex = INDEX_NONE;
	UPROPERTY(SaveGame) int32 DisplayWaveNumber = INDEX_NONE;
	UPROPERTY(SaveGame) int32 AliveEnemyCount = 0;
	UPROPERTY(SaveGame) int32 SpawnSerialCounter = 0;
	UPROPERTY(SaveGame) bool bWaveActive = false;
	UPROPERTY(SaveGame) bool bPreWaveTimerPending = false;
	UPROPERTY(SaveGame) float PreWaveRemaining = 0.f;
	UPROPERTY(SaveGame) TArray<FSWRoomWaveGroupState> Groups;
	UPROPERTY(SaveGame) TArray<FGuid> ActiveEnemyIds;
};

class ABaseEnemy;
class ASpawnRoute;
class AWaveGameMode;
class UEnemyWaypointMoveComponent;
class UWaveDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveSpawnManagerWaveStartedSignature, int32, WaveArrayIndex, int32, DisplayWaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveSpawnManagerWaveCompletedSignature, int32, WaveArrayIndex, int32, DisplayWaveNumber, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveSpawnManagerWaveStoppedSignature, int32, WaveArrayIndex, int32, DisplayWaveNumber, EWaveEndReason, EndReason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveSpawnManagerCountdownStartedSignature, int32, WaveArrayIndex, int32, DisplayWaveNumber, float, CountdownDuration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveSpawnManagerSpawnGroupFinishedSignature, int32, WaveArrayIndex, int32, SpawnGroupIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnWaveSpawnManagerEnemySpawnedSignature, ABaseEnemy*, Enemy, int32, WaveArrayIndex, int32, SpawnGroupIndex, int32, AliveEnemyCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveSpawnManagerEnemyRemovedSignature, ABaseEnemy*, Enemy, EWaveEnemyRemoveReason, Reason, int32, AliveEnemyCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveSpawnManagerRouteMoveFailedSignature, ABaseEnemy*, Enemy, ASpawnRoute*, Route, int32, FailedWaypointIndex);

/**
 * WaveSystem의 서버 권한 Spawn 실행자.
 *
 * 책임:
 * - UWaveDataAsset을 Wave 정적 데이터의 단일 소스로 사용
 * - DisplayWaveNumber -> WaveArrayIndex 변환
 * - PreWaveDelay / SpawnGroup StartDelay / SpawnInterval / BurstCount 처리
 * - ABaseEnemy Spawn
 * - Enemy Death / GoalReached / Destroyed 추적
 * - AliveEnemyCount 계산
 * - 모든 SpawnGroup 완료 + AliveEnemyCount 0이면 Wave 완료 보고
 *
 * 비책임:
 * - GameMode Phase 정책 결정
 * - Victory / Defeat 판정
 * - GameState 직접 수정
 * - 다음 Wave 시작 승인
 */
UCLASS()
class ENEMY_API AWaveSpawnManager : public AActor, public ISWRoomStateAdapter, public ISWVoyageResetParticipant
{
    GENERATED_BODY()

public:
    AWaveSpawnManager();
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const override { return ESWVoyageRestoreStage::InitialSpawners; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) override;
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) override;
    virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
    virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
    virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
        float TimeToleranceSeconds, TArray<FString>& OutFields) const override
    { return FSWRoomStructCodec::Compare<FSWRoomWaveState>(Expected, Actual, TimeToleranceSeconds, OutFields); }
    virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // -------------------- Events

    /** 실제 SpawnGroup 실행이 시작되었을 때 발생한다. PreWaveDelay 시작 시점이 아니다. */
    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerWaveStartedSignature OnWaveSpawnStarted;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerWaveCompletedSignature OnWaveSpawnCompleted;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerWaveStoppedSignature OnWaveSpawnStopped;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerCountdownStartedSignature OnWaveCountdownStarted;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerSpawnGroupFinishedSignature OnSpawnGroupFinished;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerEnemySpawnedSignature OnWaveEnemySpawned;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerEnemyRemovedSignature OnWaveEnemyRemoved;

    UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
    FOnWaveSpawnManagerRouteMoveFailedSignature OnWaveEnemyRouteMoveFailed;

public:
    // -------------------- Config

    /** Wave 정적 데이터의 단일 소스. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Data")
    TObjectPtr<UWaveDataAsset> WaveData = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Route")
    bool bAutoBuildRouteMapOnBeginPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Validation")
    bool bValidateOnBeginPlay = true;
    
    /**
     * true면 BeginPlay에서 AWaveGameMode를 찾아 OnWaveStartRequested / OnWaveStopRequested에 바인딩한다.
     * Enemy -> Core 방향 의존성만 생기므로 Core가 Enemy를 알 필요는 없다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|GameMode")
    bool bBindToWaveGameMode = true;

    /**
     * true면 WaveDataAsset의 FWaveDefinition::PreWaveDelay를 적용한다.
     * 새 WaveGameMode 구조에서는 GameMode가 FirstSpawnDelay를 들지 않으므로 기본값 true가 정석이다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Timing")
    bool bUseWavePreWaveDelay = true;

    /**
     * SpawnActor 실패 시에도 해당 Spawn Count를 소비한다.
     * false면 일시적 실패를 다음 Spawn Tick에서 재시도할 수 있지만, 영구 실패 시 Wave가 끝나지 않을 수 있다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Spawn")
    bool bConsumeSpawnCountOnSpawnFailure = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Spawn")
    ESpawnActorCollisionHandlingMethod SpawnCollisionHandleMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Route")
    bool bDestroyEnemyOnGoalReached = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Route")
    bool bRemoveEnemyOnRouteMoveFailed = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Route")
    bool bDestroyEnemyOnRouteMoveFailed = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|Seed")
    int32 EnemySeedBase = 20260509;

protected:
    // -------------------- Runtime

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
    int32 CurrentWaveArrayIndex = INDEX_NONE;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
    int32 CurrentDisplayWaveNumber = INDEX_NONE;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
    int32 AliveEnemyCount = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
    bool bWaveActive = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
    TMap<FName, TObjectPtr<ASpawnRoute>> RouteMap;

    UPROPERTY(VisibleInstanceOnly, Category = "Wave|Runtime")
    TArray<FSpawnGroupRuntime> RuntimeGroups;

    UPROPERTY(Transient)
    TObjectPtr<AWaveGameMode> CachedWaveGameMode = nullptr;

    UPROPERTY(Transient)
    FWaveDefinition CurrentWaveDefinition;

private:
	TArray<FTimerHandle> PausedVoyageTimers;
	int32 RestoredVoyageGeneration = INDEX_NONE;
	int32 ResumedVoyageGeneration = INDEX_NONE;
    TSet<TWeakObjectPtr<ABaseEnemy>> ActiveEnemies;
    TSet<TWeakObjectPtr<ABaseEnemy>> RemovedEnemies;

    FTimerHandle PreWaveDelayTimerHandle;

    int32 SpawnSerialCounter = 0;
    FSWRoomWaveState PendingRoomState;
    bool bHasPendingRoomState = false;

public:
    // -------------------- Public API

    UFUNCTION(BlueprintCallable, Category = "Wave|Route")
    bool BuildRouteMap();

    UFUNCTION(BlueprintCallable, Category = "Wave|Validation")
    bool ValidateManagerSetup() const;

    UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
    bool StartWaveByArrayIndex(int32 WaveArrayIndex);

    UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
    bool StartWaveByDisplayNumber(int32 DisplayWaveNumber);

    /**
     * 현재 Wave를 중단한다.
     * bReportToGameMode=false는 GameMode의 OnWaveStopRequested를 처리할 때 되먹임을 막기 위해 사용한다.
     */
    UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
    void StopActiveWave(
        EWaveEndReason EndReason = EWaveEndReason::TimeExpired,
        bool bDespawnAliveEnemies = true,
        bool bReportToGameMode = true
    );

    UFUNCTION(BlueprintCallable, Category = "Wave|Enemy")
    void NotifyEnemyRemovedFromWave(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);

    UFUNCTION(BlueprintPure, Category = "Wave|Runtime")
    bool IsWaveActive() const;

    UFUNCTION(BlueprintPure, Category = "Wave|Runtime")
    int32 GetCurrentWaveArrayIndex() const;

    UFUNCTION(BlueprintPure, Category = "Wave|Runtime")
    int32 GetCurrentDisplayWaveNumber() const;

    UFUNCTION(BlueprintPure, Category = "Wave|Runtime")
    int32 GetAliveEnemyCount() const;

    UFUNCTION(BlueprintPure, Category = "Wave|Route")
    ASpawnRoute* FindRouteById(FName RouteId) const;

private:
    // -------------------- GameMode binding

    void BindToWaveGameMode();
    void UnbindFromWaveGameMode();

    UFUNCTION()
    void HandleWaveStartRequested(int32 DisplayWaveNumber);

    UFUNCTION()
    void HandleWaveStopRequested(int32 DisplayWaveNumber, EWaveEndReason EndReason);

    // -------------------- WaveFlowInterface report helpers

    UObject* GetWaveFlowObject() const;
    bool DoesWaveFlowExist() const;

    void ReportWaveDataReady();
    void ReportWaveCountdownStarted(int32 WaveArrayIndex, int32 DisplayWaveNumber, float CountdownDuration);
    void ReportWaveRuntimeStarted(int32 WaveArrayIndex, int32 DisplayWaveNumber, float WaveTimeLimit);
    void ReportWaveEnemyCountChanged();
    void ReportWaveCompleted(int32 WaveArrayIndex, int32 DisplayWaveNumber, bool bSuccess, float PostWaveDelay);
    void ReportWaveStopped(int32 WaveArrayIndex, int32 DisplayWaveNumber, EWaveEndReason EndReason, float PostWaveDelay);

    // -------------------- Wave execution

    void BeginWaveSpawning();
    void BeginSpawnGroup(int32 SpawnGroupIndex);
    void SpawnBurstForGroup(int32 SpawnGroupIndex);
    void FinishSpawnGroup(int32 SpawnGroupIndex);
    void ClearAllSpawnTimers();
    bool AreAllSpawnGroupsFinished() const;
    void CheckWaveComplete();
    void CompleteWave(bool bSuccess);

    float GetPostWaveDelayByArrayIndex(int32 WaveArrayIndex) const;
    float GetWaveTimeLimitForCurrentWave() const;

    // -------------------- Enemy spawn / removal

    bool PrepareSpawnTickets(int32 SpawnGroupIndex, float DelaySeconds);
    bool SpawnOneEnemyFromGroup(int32 SpawnGroupIndex, const FSpawnGroupDefinition& SpawnGroupDefinition,
        const FSWRoomPendingSpawnTicket& Ticket);
    int32 GenerateEnemyRouteSeed(int32 WaveArrayIndex, int32 SpawnGroupIndex, int32 SpawnOrdinalInGroup);
    UEnemyWaypointMoveComponent* ResolveWaypointMoveComponent(ABaseEnemy* Enemy) const;
    void BindEnemyDelegates(ABaseEnemy* Enemy, UEnemyWaypointMoveComponent* WaypointMoveComponent);
    void UnbindEnemyDelegates(ABaseEnemy* Enemy);

    UFUNCTION()
    void HandleEnemyDeathNotified(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);

    UFUNCTION()
    void HandleRouteGoalReached(AActor* EnemyActor, ASpawnRoute* Route);

    UFUNCTION()
    void HandleRouteMoveFailed(AActor* EnemyActor, ASpawnRoute* Route, int32 FailedWaypointIndex);

    UFUNCTION()
    void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);
};
