#include "WaveSystem/Manager/WaveSpawnManager.h"

#include "BaseEnemy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WaveGameMode.h"
#include "Interactable/WaveFlowInterface.h"
#include "TimerManager.h"
#include "WaveSystem/Data/WaveDataAsset.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"
#include "WaveSystem/Route/SpawnRoute.h"

DEFINE_LOG_CATEGORY_STATIC(LogWaveSpawnManager, Log, All);

AWaveSpawnManager::AWaveSpawnManager()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
}

void AWaveSpawnManager::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority())
    {
        return;
    }

    if (bAutoBuildRouteMapOnBeginPlay)
    {
        BuildRouteMap();
    }

    const bool bSetupValid = !bValidateOnBeginPlay || ValidateManagerSetup();

    if (bBindToWaveGameMode)
    {
        BindToWaveGameMode();
    }

    if (bSetupValid)
    {
        ReportWaveDataReady();
    }
}

void AWaveSpawnManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        UnbindFromWaveGameMode();
        StopActiveWave(EWaveEndReason::TimeExpired, true, false);
        ClearAllSpawnTimers();
    }

    Super::EndPlay(EndPlayReason);
}

bool AWaveSpawnManager::BuildRouteMap()
{
    RouteMap.Reset();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] BuildRouteMap failed: World is null. Manager=%s"), *GetNameSafe(this));
        return false;
    }

    bool bSuccess = true;

    for (TActorIterator<ASpawnRoute> It(World); It; ++It)
    {
        ASpawnRoute* Route = *It;
        if (!IsValid(Route))
        {
            continue;
        }

        if (Route->RouteId.IsNone())
        {
            UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] SpawnRoute has None RouteId and will be ignored. Route=%s"), *GetNameSafe(Route));
            bSuccess = false;
            continue;
        }

        if (RouteMap.Contains(Route->RouteId))
        {
            UE_LOG(
                LogWaveSpawnManager,
                Error,
                TEXT("[WaveSpawnManager] Duplicate RouteId detected. RouteId=%s Existing=%s Duplicate=%s"),
                *Route->RouteId.ToString(),
                *GetNameSafe(RouteMap[Route->RouteId]),
                *GetNameSafe(Route)
            );
            bSuccess = false;
            continue;
        }

        RouteMap.Add(Route->RouteId, Route);
    }

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] BuildRouteMap completed. Manager=%s RouteCount=%d Success=%s"),
        *GetNameSafe(this),
        RouteMap.Num(),
        bSuccess ? TEXT("true") : TEXT("false")
    );

    return bSuccess;
}

bool AWaveSpawnManager::ValidateManagerSetup() const
{
    bool bSuccess = true;

    if (!WaveData)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] Validation failed: WaveData is null. Manager=%s"), *GetNameSafe(this));
        return false;
    }

    TArray<FString> WaveDataErrors;
    if (!WaveData->ValidateWaveData(WaveDataErrors))
    {
        bSuccess = false;

        UE_LOG(
            LogWaveSpawnManager,
            Error,
            TEXT("[WaveSpawnManager] WaveData validation failed. Asset=%s ErrorCount=%d"),
            *GetNameSafe(WaveData),
            WaveDataErrors.Num()
        );

        for (const FString& Error : WaveDataErrors)
        {
            UE_LOG(LogWaveSpawnManager, Error, TEXT("%s"), *Error);
        }
    }

    if (RouteMap.Num() <= 0)
    {
        UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] RouteMap is empty. Manager=%s"), *GetNameSafe(this));
        bSuccess = false;
    }

    for (int32 WaveIndex = 0; WaveIndex < WaveData->WaveDefinitions.Num(); ++WaveIndex)
    {
        const FWaveDefinition& WaveDefinition = WaveData->WaveDefinitions[WaveIndex];

        for (int32 GroupIndex = 0; GroupIndex < WaveDefinition.SpawnGroups.Num(); ++GroupIndex)
        {
            const FSpawnGroupDefinition& GroupDefinition = WaveDefinition.SpawnGroups[GroupIndex];

            if (!GroupDefinition.RouteId.IsNone() && !RouteMap.Contains(GroupDefinition.RouteId))
            {
                UE_LOG(
                    LogWaveSpawnManager,
                    Error,
                    TEXT("[WaveSpawnManager] RouteId not found. WaveArrayIndex=%d DisplayWaveNumber=%d GroupIndex=%d RouteId=%s"),
                    WaveIndex,
                    WaveDefinition.DisplayWaveNumber,
                    GroupIndex,
                    *GroupDefinition.RouteId.ToString()
                );
                bSuccess = false;
            }
        }
    }

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] ValidateManagerSetup finished. Manager=%s Success=%s"),
        *GetNameSafe(this),
        bSuccess ? TEXT("true") : TEXT("false")
    );

    return bSuccess;
}

bool AWaveSpawnManager::StartWaveByArrayIndex(int32 WaveArrayIndex)
{
    if (!HasAuthority())
    {
        return false;
    }

    if (!WaveData)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] StartWaveByArrayIndex failed: WaveData is null. Manager=%s"), *GetNameSafe(this));
        ReportWaveCompleted(INDEX_NONE, INDEX_NONE, false, 0.f);
        return false;
    }

    if (!WaveData->IsValidWaveIndex(WaveArrayIndex))
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] StartWaveByArrayIndex failed: invalid WaveArrayIndex=%d Asset=%s"), WaveArrayIndex, *GetNameSafe(WaveData));
        ReportWaveCompleted(WaveArrayIndex, INDEX_NONE, false, 0.f);
        return false;
    }

    const FWaveDefinition& RequestedWaveDefinition = WaveData->GetWaveDefinitionChecked(WaveArrayIndex);

    if (RouteMap.Num() <= 0 && bAutoBuildRouteMapOnBeginPlay)
    {
        BuildRouteMap();
    }

    if (bWaveActive)
    {
        UE_LOG(
            LogWaveSpawnManager,
            Warning,
            TEXT("[WaveSpawnManager] StartWaveByArrayIndex requested while another wave is active. PreviousWaveArrayIndex=%d NewWaveArrayIndex=%d"),
            CurrentWaveArrayIndex,
            WaveArrayIndex
        );
        StopActiveWave(EWaveEndReason::TimeExpired, true, false);
    }

    CurrentWaveDefinition = RequestedWaveDefinition;
    CurrentWaveArrayIndex = WaveArrayIndex;
    CurrentDisplayWaveNumber = CurrentWaveDefinition.DisplayWaveNumber;
    AliveEnemyCount = 0;
    SpawnSerialCounter = 0;
    bWaveActive = true;

    ActiveEnemies.Reset();
    RemovedEnemies.Reset();
    RuntimeGroups.Reset();
    RuntimeGroups.SetNum(CurrentWaveDefinition.SpawnGroups.Num());

    for (int32 GroupIndex = 0; GroupIndex < CurrentWaveDefinition.SpawnGroups.Num(); ++GroupIndex)
    {
        RuntimeGroups[GroupIndex].InitializeFromDefinition(CurrentWaveDefinition.SpawnGroups[GroupIndex]);
    }

    const float PreWaveDelay = bUseWavePreWaveDelay
        ? FMath::Max(0.0f, CurrentWaveDefinition.PreWaveDelay)
        : 0.0f;

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Wave requested. WaveArrayIndex=%d DisplayWaveNumber=%d SpawnGroupCount=%d PreWaveDelay=%.2f"),
        CurrentWaveArrayIndex,
        CurrentDisplayWaveNumber,
        RuntimeGroups.Num(),
        PreWaveDelay
    );

    ReportWaveCountdownStarted(CurrentWaveArrayIndex, CurrentDisplayWaveNumber, PreWaveDelay);
    ReportWaveEnemyCountChanged();
    OnWaveCountdownStarted.Broadcast(CurrentWaveArrayIndex, CurrentDisplayWaveNumber, PreWaveDelay);

    if (PreWaveDelay > 0.0f)
    {
        GetWorldTimerManager().ClearTimer(PreWaveDelayTimerHandle);
        GetWorldTimerManager().SetTimer(
            PreWaveDelayTimerHandle,
            this,
            &AWaveSpawnManager::BeginWaveSpawning,
            PreWaveDelay,
            false
        );
    }
    else
    {
        BeginWaveSpawning();
    }

    return true;
}

bool AWaveSpawnManager::StartWaveByDisplayNumber(int32 DisplayWaveNumber)
{
    if (!HasAuthority())
    {
        return false;
    }

    if (!WaveData)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] StartWaveByDisplayNumber failed: WaveData is null. DisplayWaveNumber=%d"), DisplayWaveNumber);
        ReportWaveCompleted(INDEX_NONE, DisplayWaveNumber, false, 0.f);
        return false;
    }

    int32 WaveArrayIndex = INDEX_NONE;
    if (!WaveData->FindWaveIndexByDisplayNumber(DisplayWaveNumber, WaveArrayIndex))
    {
        UE_LOG(
            LogWaveSpawnManager,
            Error,
            TEXT("[WaveSpawnManager] StartWaveByDisplayNumber failed: DisplayWaveNumber not found. DisplayWaveNumber=%d Asset=%s"),
            DisplayWaveNumber,
            *GetNameSafe(WaveData)
        );
        ReportWaveCompleted(INDEX_NONE, DisplayWaveNumber, false, 0.f);
        return false;
    }

    return StartWaveByArrayIndex(WaveArrayIndex);
}

void AWaveSpawnManager::StopActiveWave(EWaveEndReason EndReason, bool bDespawnAliveEnemies, bool bReportToGameMode)
{
    if (!HasAuthority())
    {
        return;
    }

    const bool bHadWave = bWaveActive || CurrentWaveArrayIndex != INDEX_NONE;
    if (!bHadWave)
    {
        return;
    }

    const int32 StoppedWaveArrayIndex = CurrentWaveArrayIndex;
    const int32 StoppedDisplayWaveNumber = CurrentDisplayWaveNumber;
    const float PostWaveDelay = GetPostWaveDelayByArrayIndex(StoppedWaveArrayIndex);

    bWaveActive = false;
    ClearAllSpawnTimers();

    for (FSpawnGroupRuntime& RuntimeGroup : RuntimeGroups)
    {
        if (!RuntimeGroup.bFinished)
        {
            RuntimeGroup.bFinished = true;
            RuntimeGroup.State = EWaveSpawnGroupState::Cancelled;
        }
    }

    if (bDespawnAliveEnemies)
    {
        TArray<TWeakObjectPtr<ABaseEnemy>> EnemiesToDespawn = ActiveEnemies.Array();
        for (const TWeakObjectPtr<ABaseEnemy>& EnemyPtr : EnemiesToDespawn)
        {
            ABaseEnemy* Enemy = EnemyPtr.Get();
            if (!IsValid(Enemy))
            {
                continue;
            }

            NotifyEnemyRemovedFromWave(Enemy, EWaveEnemyRemoveReason::Despawn);
            Enemy->Destroy();
        }
    }
    else
    {
        TArray<TWeakObjectPtr<ABaseEnemy>> EnemiesToUnbind = ActiveEnemies.Array();
        for (const TWeakObjectPtr<ABaseEnemy>& EnemyPtr : EnemiesToUnbind)
        {
            if (ABaseEnemy* Enemy = EnemyPtr.Get())
            {
                UnbindEnemyDelegates(Enemy);
            }
        }
    }

    ActiveEnemies.Reset();
    RemovedEnemies.Reset();
    RuntimeGroups.Reset();
    CurrentWaveDefinition = FWaveDefinition();
    CurrentWaveArrayIndex = INDEX_NONE;
    CurrentDisplayWaveNumber = INDEX_NONE;
    AliveEnemyCount = 0;
    SpawnSerialCounter = 0;

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Wave stopped. WaveArrayIndex=%d DisplayWaveNumber=%d EndReason=%d"),
        StoppedWaveArrayIndex,
        StoppedDisplayWaveNumber,
        static_cast<int32>(EndReason)
    );

    OnWaveSpawnStopped.Broadcast(StoppedWaveArrayIndex, StoppedDisplayWaveNumber, EndReason);
    ReportWaveEnemyCountChanged();

    if (bReportToGameMode)
    {
        ReportWaveStopped(StoppedWaveArrayIndex, StoppedDisplayWaveNumber, EndReason, PostWaveDelay);
    }
}

void AWaveSpawnManager::NotifyEnemyRemovedFromWave(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason)
{
    if (!HasAuthority() || !IsValid(Enemy))
    {
        return;
    }

    const TWeakObjectPtr<ABaseEnemy> EnemyKey(Enemy);

    if (RemovedEnemies.Contains(EnemyKey))
    {
        UE_LOG(LogWaveSpawnManager, Verbose, TEXT("[WaveSpawnManager] Duplicate enemy removal ignored. Enemy=%s Reason=%d"), *GetNameSafe(Enemy), static_cast<int32>(Reason));
        return;
    }

    if (!ActiveEnemies.Contains(EnemyKey))
    {
        RemovedEnemies.Add(EnemyKey);
        UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] Enemy removal ignored because enemy is not tracked. Enemy=%s Reason=%d"), *GetNameSafe(Enemy), static_cast<int32>(Reason));
        return;
    }

    RemovedEnemies.Add(EnemyKey);
    ActiveEnemies.Remove(EnemyKey);

    UnbindEnemyDelegates(Enemy);

    if (UEnemyWaypointMoveComponent* WaypointMoveComponent = ResolveWaypointMoveComponent(Enemy))
    {
        WaypointMoveComponent->StopRoute(false);
    }

    AliveEnemyCount = FMath::Max(AliveEnemyCount - 1, 0);

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Enemy removed. Enemy=%s Reason=%d AliveEnemyCount=%d WaveArrayIndex=%d"),
        *GetNameSafe(Enemy),
        static_cast<int32>(Reason),
        AliveEnemyCount,
        CurrentWaveArrayIndex
    );

    OnWaveEnemyRemoved.Broadcast(Enemy, Reason, AliveEnemyCount);
    ReportWaveEnemyCountChanged();

    if (bWaveActive)
    {
        CheckWaveComplete();
    }
}

bool AWaveSpawnManager::IsWaveActive() const
{
    return bWaveActive;
}

int32 AWaveSpawnManager::GetCurrentWaveArrayIndex() const
{
    return CurrentWaveArrayIndex;
}

int32 AWaveSpawnManager::GetCurrentDisplayWaveNumber() const
{
    return CurrentDisplayWaveNumber;
}

int32 AWaveSpawnManager::GetAliveEnemyCount() const
{
    return AliveEnemyCount;
}

ASpawnRoute* AWaveSpawnManager::FindRouteById(FName RouteId) const
{
    const TObjectPtr<ASpawnRoute>* FoundRoute = RouteMap.Find(RouteId);
    return FoundRoute ? FoundRoute->Get() : nullptr;
}

void AWaveSpawnManager::ApplySpawnGroupModifiers_Implementation(ABaseEnemy* Enemy, const FSpawnGroupDefinition& SpawnGroupDefinition)
{
    if (!IsValid(Enemy))
    {
        return;
    }

    if (!FMath::IsNearlyEqual(SpawnGroupDefinition.SpeedMultiplier, 1.0f))
    {
        if (UCharacterMovementComponent* MovementComponent = Enemy->FindComponentByClass<UCharacterMovementComponent>())
        {
            MovementComponent->MaxWalkSpeed *= SpawnGroupDefinition.SpeedMultiplier;
        }
    }
}

void AWaveSpawnManager::BindToWaveGameMode()
{
    if (!HasAuthority())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UnbindFromWaveGameMode();

    CachedWaveGameMode = World->GetAuthGameMode<AWaveGameMode>();
    if (!CachedWaveGameMode)
    {
        UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] AWaveGameMode not found. Manager=%s"), *GetNameSafe(this));
        return;
    }

    CachedWaveGameMode->OnWaveStartRequested.AddDynamic(this, &AWaveSpawnManager::HandleWaveStartRequested);
    CachedWaveGameMode->OnWaveStopRequested.AddDynamic(this, &AWaveSpawnManager::HandleWaveStopRequested);

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Bound to AWaveGameMode. Manager=%s GameMode=%s"),
        *GetNameSafe(this),
        *GetNameSafe(CachedWaveGameMode)
    );
}

void AWaveSpawnManager::UnbindFromWaveGameMode()
{
    if (!CachedWaveGameMode)
    {
        return;
    }

    CachedWaveGameMode->OnWaveStartRequested.RemoveDynamic(this, &AWaveSpawnManager::HandleWaveStartRequested);
    CachedWaveGameMode->OnWaveStopRequested.RemoveDynamic(this, &AWaveSpawnManager::HandleWaveStopRequested);
    CachedWaveGameMode = nullptr;
}

void AWaveSpawnManager::HandleWaveStartRequested(int32 DisplayWaveNumber)
{
    UE_LOG(LogWaveSpawnManager, Log, TEXT("[WaveSpawnManager] Received WaveGameMode OnWaveStartRequested. DisplayWaveNumber=%d"), DisplayWaveNumber);
    StartWaveByDisplayNumber(DisplayWaveNumber);
}

void AWaveSpawnManager::HandleWaveStopRequested(int32 DisplayWaveNumber, EWaveEndReason EndReason)
{
    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Received WaveGameMode OnWaveStopRequested. DisplayWaveNumber=%d EndReason=%d"),
        DisplayWaveNumber,
        static_cast<int32>(EndReason)
    );

    if (!bWaveActive)
    {
        ClearAllSpawnTimers();
        return;
    }

    if (CurrentDisplayWaveNumber != INDEX_NONE && CurrentDisplayWaveNumber != DisplayWaveNumber)
    {
        return;
    }

    // GameMode가 요청한 중단이므로 Interface로 다시 NotifyWaveStopped를 보내지 않는다.
    // 그렇지 않으면 HandleDefeat 같은 흐름에서 GameMode가 WaveStopped 보고를 다시 받아 Intermission을 시작할 수 있다.
    StopActiveWave(EndReason, true, false);
}

UObject* AWaveSpawnManager::GetWaveFlowObject() const
{
    if (CachedWaveGameMode && CachedWaveGameMode->GetClass()->ImplementsInterface(UWaveFlowInterface::StaticClass()))
    {
        return CachedWaveGameMode.Get();
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    AGameModeBase* GameMode = World->GetAuthGameMode();
    if (!GameMode)
    {
        return nullptr;
    }

    if (!GameMode->GetClass()->ImplementsInterface(UWaveFlowInterface::StaticClass()))
    {
        return nullptr;
    }

    return GameMode;
}

bool AWaveSpawnManager::DoesWaveFlowExist() const
{
    return GetWaveFlowObject() != nullptr;
}

void AWaveSpawnManager::ReportWaveDataReady()
{
    if (!WaveData)
    {
        return;
    }

    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveDataReady(WaveFlowObject, WaveData->GetWaveCount());
}

void AWaveSpawnManager::ReportWaveCountdownStarted(int32 WaveArrayIndex, int32 DisplayWaveNumber, float CountdownDuration)
{
    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveCountdownStarted(
        WaveFlowObject,
        DisplayWaveNumber,
        CountdownDuration
    );
}

void AWaveSpawnManager::ReportWaveRuntimeStarted(int32 WaveArrayIndex, int32 DisplayWaveNumber, float WaveTimeLimit)
{
    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveRuntimeStarted(
        WaveFlowObject,
        WaveArrayIndex,
        DisplayWaveNumber,
        WaveTimeLimit
    );
}

void AWaveSpawnManager::ReportWaveEnemyCountChanged()
{
    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveEnemyCountChanged(WaveFlowObject, AliveEnemyCount);
}

void AWaveSpawnManager::ReportWaveCompleted(int32 WaveArrayIndex, int32 DisplayWaveNumber, bool bSuccess, float PostWaveDelay)
{
    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveCompleted(
        WaveFlowObject,
        WaveArrayIndex,
        DisplayWaveNumber,
        bSuccess,
        PostWaveDelay
    );
}

void AWaveSpawnManager::ReportWaveStopped(int32 WaveArrayIndex, int32 DisplayWaveNumber, EWaveEndReason EndReason, float PostWaveDelay)
{
    UObject* WaveFlowObject = GetWaveFlowObject();
    if (!WaveFlowObject)
    {
        return;
    }

    IWaveFlowInterface::Execute_NotifyWaveStopped(
        WaveFlowObject,
        WaveArrayIndex,
        DisplayWaveNumber,
        EndReason,
        PostWaveDelay
    );
}

void AWaveSpawnManager::BeginWaveSpawning()
{
    if (!bWaveActive)
    {
        return;
    }

    GetWorldTimerManager().ClearTimer(PreWaveDelayTimerHandle);

    const float WaveTimeLimit = GetWaveTimeLimitForCurrentWave();

    ReportWaveRuntimeStarted(CurrentWaveArrayIndex, CurrentDisplayWaveNumber, WaveTimeLimit);
    OnWaveSpawnStarted.Broadcast(CurrentWaveArrayIndex, CurrentDisplayWaveNumber);

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Wave runtime started. WaveArrayIndex=%d DisplayWaveNumber=%d WaveTimeLimit=%.2f"),
        CurrentWaveArrayIndex,
        CurrentDisplayWaveNumber,
        WaveTimeLimit
    );

    if (RuntimeGroups.Num() <= 0)
    {
        UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] BeginWaveSpawning: RuntimeGroups is empty. WaveArrayIndex=%d"), CurrentWaveArrayIndex);
        CheckWaveComplete();
        return;
    }

    for (int32 GroupIndex = 0; GroupIndex < RuntimeGroups.Num(); ++GroupIndex)
    {
        FSpawnGroupRuntime& RuntimeGroup = RuntimeGroups[GroupIndex];
        if (RuntimeGroup.bFinished)
        {
            continue;
        }

        const FSpawnGroupDefinition& GroupDefinition = CurrentWaveDefinition.SpawnGroups[GroupIndex];
        const float StartDelay = FMath::Max(0.0f, GroupDefinition.StartDelay);

        if (StartDelay <= 0.0f)
        {
            BeginSpawnGroup(GroupIndex);
        }
        else
        {
            FTimerDelegate TimerDelegate;
            TimerDelegate.BindUObject(this, &AWaveSpawnManager::BeginSpawnGroup, GroupIndex);
            GetWorldTimerManager().SetTimer(RuntimeGroup.TimerHandle, TimerDelegate, StartDelay, false);

            UE_LOG(
                LogWaveSpawnManager,
                Log,
                TEXT("[WaveSpawnManager] SpawnGroup scheduled. WaveArrayIndex=%d GroupIndex=%d StartDelay=%.2f"),
                CurrentWaveArrayIndex,
                GroupIndex,
                StartDelay
            );
        }
    }

    CheckWaveComplete();
}

void AWaveSpawnManager::BeginSpawnGroup(int32 SpawnGroupIndex)
{
    if (!bWaveActive || !RuntimeGroups.IsValidIndex(SpawnGroupIndex) || !CurrentWaveDefinition.SpawnGroups.IsValidIndex(SpawnGroupIndex))
    {
        return;
    }

    FSpawnGroupRuntime& RuntimeGroup = RuntimeGroups[SpawnGroupIndex];
    if (RuntimeGroup.bFinished)
    {
        return;
    }

    RuntimeGroup.State = EWaveSpawnGroupState::Spawning;
    GetWorldTimerManager().ClearTimer(RuntimeGroup.TimerHandle);

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] SpawnGroup started. WaveArrayIndex=%d GroupIndex=%d Remaining=%d"),
        CurrentWaveArrayIndex,
        SpawnGroupIndex,
        RuntimeGroup.RemainingCount
    );

    SpawnBurstForGroup(SpawnGroupIndex);
}

void AWaveSpawnManager::SpawnBurstForGroup(int32 SpawnGroupIndex)
{
    if (!bWaveActive || !RuntimeGroups.IsValidIndex(SpawnGroupIndex))
    {
        return;
    }

    FSpawnGroupRuntime& RuntimeGroup = RuntimeGroups[SpawnGroupIndex];
    const FSpawnGroupDefinition& GroupDefinition = CurrentWaveDefinition.SpawnGroups[SpawnGroupIndex];

    if (RuntimeGroup.RemainingCount <= 0)
    {
        FinishSpawnGroup(SpawnGroupIndex);
        return;
    }

    const bool bSpawnAllImmediately = GroupDefinition.SpawnInterval <= 0.0f;

    do
    {
        const int32 BurstCount = FMath::Max(1, GroupDefinition.BurstCount);
        int32 SpawnedThisBurst = 0;

        while (RuntimeGroup.RemainingCount > 0 && SpawnedThisBurst < BurstCount)
        {
            const int32 SpawnOrdinalInGroup = RuntimeGroup.SpawnedCount;
            const bool bSpawned = SpawnOneEnemyFromGroup(
                SpawnGroupIndex,
                GroupDefinition,
                SpawnOrdinalInGroup
            );

            if (bSpawned || bConsumeSpawnCountOnSpawnFailure)
            {
                --RuntimeGroup.RemainingCount;
                ++RuntimeGroup.SpawnedCount;
            }
            else
            {
                UE_LOG(
                    LogWaveSpawnManager,
                    Warning,
                    TEXT("[WaveSpawnManager] Spawn failed without consuming count. Will retry next interval. WaveArrayIndex=%d GroupIndex=%d Remaining=%d"),
                    CurrentWaveArrayIndex,
                    SpawnGroupIndex,
                    RuntimeGroup.RemainingCount
                );
                break;
            }

            ++SpawnedThisBurst;
        }

        if (!bSpawnAllImmediately)
        {
            break;
        }

    } while (RuntimeGroup.RemainingCount > 0);

    if (RuntimeGroup.RemainingCount <= 0)
    {
        FinishSpawnGroup(SpawnGroupIndex);
        return;
    }

    FTimerDelegate TimerDelegate;
    TimerDelegate.BindUObject(this, &AWaveSpawnManager::SpawnBurstForGroup, SpawnGroupIndex);
    GetWorldTimerManager().SetTimer(
        RuntimeGroup.TimerHandle,
        TimerDelegate,
        GroupDefinition.SpawnInterval,
        false
    );
}

void AWaveSpawnManager::FinishSpawnGroup(int32 SpawnGroupIndex)
{
    if (!RuntimeGroups.IsValidIndex(SpawnGroupIndex))
    {
        return;
    }

    FSpawnGroupRuntime& RuntimeGroup = RuntimeGroups[SpawnGroupIndex];
    GetWorldTimerManager().ClearTimer(RuntimeGroup.TimerHandle);

    RuntimeGroup.RemainingCount = 0;
    RuntimeGroup.bFinished = true;
    RuntimeGroup.State = EWaveSpawnGroupState::Finished;

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] SpawnGroup finished. WaveArrayIndex=%d GroupIndex=%d SpawnedCount=%d AliveEnemyCount=%d"),
        CurrentWaveArrayIndex,
        SpawnGroupIndex,
        RuntimeGroup.SpawnedCount,
        AliveEnemyCount
    );

    OnSpawnGroupFinished.Broadcast(CurrentWaveArrayIndex, SpawnGroupIndex);
    CheckWaveComplete();
}

void AWaveSpawnManager::ClearAllSpawnTimers()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FTimerManager& TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(PreWaveDelayTimerHandle);

    for (FSpawnGroupRuntime& RuntimeGroup : RuntimeGroups)
    {
        TimerManager.ClearTimer(RuntimeGroup.TimerHandle);
    }
}

bool AWaveSpawnManager::AreAllSpawnGroupsFinished() const
{
    for (const FSpawnGroupRuntime& RuntimeGroup : RuntimeGroups)
    {
        if (!RuntimeGroup.bFinished)
        {
            return false;
        }
    }

    return true;
}

void AWaveSpawnManager::CheckWaveComplete()
{
    if (!bWaveActive)
    {
        return;
    }

    if (!AreAllSpawnGroupsFinished())
    {
        return;
    }

    if (AliveEnemyCount > 0)
    {
        return;
    }

    CompleteWave(true);
}

void AWaveSpawnManager::CompleteWave(bool bSuccess)
{
    if (!bWaveActive)
    {
        return;
    }

    const int32 CompletedWaveArrayIndex = CurrentWaveArrayIndex;
    const int32 CompletedDisplayWaveNumber = CurrentDisplayWaveNumber;
    const float PostWaveDelay = GetPostWaveDelayByArrayIndex(CompletedWaveArrayIndex);

    bWaveActive = false;
    ClearAllSpawnTimers();

    ActiveEnemies.Reset();
    RemovedEnemies.Reset();
    RuntimeGroups.Reset();
    CurrentWaveDefinition = FWaveDefinition();
    CurrentWaveArrayIndex = INDEX_NONE;
    CurrentDisplayWaveNumber = INDEX_NONE;
    AliveEnemyCount = 0;
    SpawnSerialCounter = 0;

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Wave completed. WaveArrayIndex=%d DisplayWaveNumber=%d Success=%s PostWaveDelay=%.2f"),
        CompletedWaveArrayIndex,
        CompletedDisplayWaveNumber,
        bSuccess ? TEXT("true") : TEXT("false"),
        PostWaveDelay
    );

    OnWaveSpawnCompleted.Broadcast(CompletedWaveArrayIndex, CompletedDisplayWaveNumber, bSuccess);
    ReportWaveEnemyCountChanged();
    ReportWaveCompleted(CompletedWaveArrayIndex, CompletedDisplayWaveNumber, bSuccess, PostWaveDelay);
}

float AWaveSpawnManager::GetPostWaveDelayByArrayIndex(int32 WaveArrayIndex) const
{
    if (!WaveData || !WaveData->IsValidWaveIndex(WaveArrayIndex))
    {
        return 0.f;
    }

    const FWaveDefinition& WaveDefinition = WaveData->GetWaveDefinitionChecked(WaveArrayIndex);
    return FMath::Max(0.f, WaveDefinition.NextWaveDelay);
}

float AWaveSpawnManager::GetWaveTimeLimitForCurrentWave() const
{
    // 현재 FWaveDefinition에는 WaveTimeLimit 필드가 없다.
    // 이후 WaveSpawnTypes.h에 float WaveTimeLimit을 추가하면 여기서 해당 값을 반환하면 된다.
    // 0.f는 WaveGameMode에서 "시간 제한 없음"으로 취급된다.
    return 0.f;
}

bool AWaveSpawnManager::SpawnOneEnemyFromGroup(int32 SpawnGroupIndex, const FSpawnGroupDefinition& SpawnGroupDefinition, int32 SpawnOrdinalInGroup)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    if (!SpawnGroupDefinition.EnemyClass)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] Spawn failed: EnemyClass is null. WaveArrayIndex=%d GroupIndex=%d"), CurrentWaveArrayIndex, SpawnGroupIndex);
        return false;
    }

    ASpawnRoute* Route = FindRouteById(SpawnGroupDefinition.RouteId);
    if (!IsValid(Route))
    {
        UE_LOG(
            LogWaveSpawnManager,
            Error,
            TEXT("[WaveSpawnManager] Spawn failed: RouteId not found. WaveArrayIndex=%d GroupIndex=%d RouteId=%s"),
            CurrentWaveArrayIndex,
            SpawnGroupIndex,
            *SpawnGroupDefinition.RouteId.ToString()
        );
        return false;
    }

    const FTransform SpawnTransform = Route->GetSpawnTransform(SpawnGroupDefinition.SpawnRadius);

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = this;
    SpawnParameters.SpawnCollisionHandlingOverride = SpawnCollisionHandlingMethod;

    ABaseEnemy* SpawnedEnemy = World->SpawnActor<ABaseEnemy>(
        SpawnGroupDefinition.EnemyClass,
        SpawnTransform,
        SpawnParameters
    );

    if (!IsValid(SpawnedEnemy))
    {
        UE_LOG(
            LogWaveSpawnManager,
            Error,
            TEXT("[WaveSpawnManager] SpawnActor returned null. WaveArrayIndex=%d GroupIndex=%d EnemyClass=%s"),
            CurrentWaveArrayIndex,
            SpawnGroupIndex,
            *GetNameSafe(SpawnGroupDefinition.EnemyClass.Get())
        );
        return false;
    }

    SpawnedEnemy->SpawnDefaultController();

    const int32 EnemySeed = GenerateEnemyRouteSeed(CurrentWaveArrayIndex, SpawnGroupIndex, SpawnOrdinalInGroup);
    UEnemyWaypointMoveComponent* WaypointMoveComponent = ResolveWaypointMoveComponent(SpawnedEnemy);

    const TWeakObjectPtr<ABaseEnemy> EnemyKey(SpawnedEnemy);
    ActiveEnemies.Add(EnemyKey);
    RemovedEnemies.Remove(EnemyKey);
    ++AliveEnemyCount;

    BindEnemyDelegates(SpawnedEnemy, WaypointMoveComponent);
    ApplySpawnGroupModifiers(SpawnedEnemy, SpawnGroupDefinition);

    UE_LOG(
        LogWaveSpawnManager,
        Log,
        TEXT("[WaveSpawnManager] Enemy spawned. Enemy=%s WaveArrayIndex=%d GroupIndex=%d AliveEnemyCount=%d RouteId=%s Seed=%d"),
        *GetNameSafe(SpawnedEnemy),
        CurrentWaveArrayIndex,
        SpawnGroupIndex,
        AliveEnemyCount,
        *SpawnGroupDefinition.RouteId.ToString(),
        EnemySeed
    );

    OnWaveEnemySpawned.Broadcast(SpawnedEnemy, CurrentWaveArrayIndex, SpawnGroupIndex, AliveEnemyCount);
    ReportWaveEnemyCountChanged();

    bool bRouteStarted = false;
    if (WaypointMoveComponent)
    {
        bRouteStarted = WaypointMoveComponent->StartRoute(Route, EnemySeed);
    }
    else
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] Spawned enemy has no UEnemyWaypointMoveComponent. Enemy=%s"), *GetNameSafe(SpawnedEnemy));
    }

    if (!bRouteStarted)
    {
        UE_LOG(LogWaveSpawnManager, Error, TEXT("[WaveSpawnManager] Route start failed after spawn. Enemy=%s Route=%s"), *GetNameSafe(SpawnedEnemy), *GetNameSafe(Route));

        if (bRemoveEnemyOnRouteMoveFailed)
        {
            NotifyEnemyRemovedFromWave(SpawnedEnemy, EWaveEnemyRemoveReason::Despawn);

            if (bDestroyEnemyOnRouteMoveFailed && IsValid(SpawnedEnemy))
            {
                SpawnedEnemy->Destroy();
            }
        }
    }

    return true;
}

int32 AWaveSpawnManager::GenerateEnemyRouteSeed(int32 WaveArrayIndex, int32 SpawnGroupIndex, int32 SpawnOrdinalInGroup)
{
    const uint32 A = HashCombine(static_cast<uint32>(EnemySeedBase), static_cast<uint32>(WaveArrayIndex + 1));
    const uint32 B = HashCombine(static_cast<uint32>(SpawnGroupIndex + 1), static_cast<uint32>(SpawnOrdinalInGroup + 1));
    const uint32 C = HashCombine(A, B);
    const uint32 D = HashCombine(C, static_cast<uint32>(++SpawnSerialCounter));
    return static_cast<int32>(D & 0x7fffffff);
}

UEnemyWaypointMoveComponent* AWaveSpawnManager::ResolveWaypointMoveComponent(ABaseEnemy* Enemy) const
{
    if (!IsValid(Enemy))
    {
        return nullptr;
    }

    if (UEnemyWaypointMoveComponent* WaypointMoveComponent = Enemy->GetWaypointMoveComponent())
    {
        return WaypointMoveComponent;
    }

    return Enemy->FindComponentByClass<UEnemyWaypointMoveComponent>();
}

void AWaveSpawnManager::BindEnemyDelegates(ABaseEnemy* Enemy, UEnemyWaypointMoveComponent* WaypointMoveComponent)
{
    if (!IsValid(Enemy))
    {
        return;
    }

    Enemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AWaveSpawnManager::HandleEnemyDeathNotified);
    Enemy->OnBaseEnemyDeathNotified.AddDynamic(this, &AWaveSpawnManager::HandleEnemyDeathNotified);

    Enemy->OnDestroyed.RemoveDynamic(this, &AWaveSpawnManager::HandleTrackedEnemyDestroyed);
    Enemy->OnDestroyed.AddDynamic(this, &AWaveSpawnManager::HandleTrackedEnemyDestroyed);

    if (WaypointMoveComponent)
    {
        WaypointMoveComponent->OnRouteGoalReached.RemoveDynamic(this, &AWaveSpawnManager::HandleRouteGoalReached);
        WaypointMoveComponent->OnRouteGoalReached.AddDynamic(this, &AWaveSpawnManager::HandleRouteGoalReached);

        WaypointMoveComponent->OnRouteMoveFailed.RemoveDynamic(this, &AWaveSpawnManager::HandleRouteMoveFailed);
        WaypointMoveComponent->OnRouteMoveFailed.AddDynamic(this, &AWaveSpawnManager::HandleRouteMoveFailed);
    }
}

void AWaveSpawnManager::UnbindEnemyDelegates(ABaseEnemy* Enemy)
{
    if (!IsValid(Enemy))
    {
        return;
    }

    Enemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AWaveSpawnManager::HandleEnemyDeathNotified);
    Enemy->OnDestroyed.RemoveDynamic(this, &AWaveSpawnManager::HandleTrackedEnemyDestroyed);

    if (UEnemyWaypointMoveComponent* WaypointMoveComponent = ResolveWaypointMoveComponent(Enemy))
    {
        WaypointMoveComponent->OnRouteGoalReached.RemoveDynamic(this, &AWaveSpawnManager::HandleRouteGoalReached);
        WaypointMoveComponent->OnRouteMoveFailed.RemoveDynamic(this, &AWaveSpawnManager::HandleRouteMoveFailed);
    }
}

void AWaveSpawnManager::HandleEnemyDeathNotified(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason)
{
    NotifyEnemyRemovedFromWave(Enemy, Reason == EWaveEnemyRemoveReason::Unknown ? EWaveEnemyRemoveReason::Death : Reason);
}

void AWaveSpawnManager::HandleRouteGoalReached(AActor* EnemyActor, ASpawnRoute* Route)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(EnemyActor);
    if (!IsValid(Enemy))
    {
        return;
    }

    UE_LOG(LogWaveSpawnManager, Log, TEXT("[WaveSpawnManager] Route goal reached. Enemy=%s Route=%s"), *GetNameSafe(Enemy), *GetNameSafe(Route));

    NotifyEnemyRemovedFromWave(Enemy, EWaveEnemyRemoveReason::GoalReached);

    if (bDestroyEnemyOnGoalReached && IsValid(Enemy))
    {
        Enemy->Destroy();
    }
}

void AWaveSpawnManager::HandleRouteMoveFailed(AActor* EnemyActor, ASpawnRoute* Route, int32 FailedWaypointIndex)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(EnemyActor);
    if (!IsValid(Enemy))
    {
        return;
    }

    UE_LOG(
        LogWaveSpawnManager,
        Warning,
        TEXT("[WaveSpawnManager] Route move failed. Enemy=%s Route=%s FailedWaypointIndex=%d"),
        *GetNameSafe(Enemy),
        *GetNameSafe(Route),
        FailedWaypointIndex
    );

    OnWaveEnemyRouteMoveFailed.Broadcast(Enemy, Route, FailedWaypointIndex);

    if (!bRemoveEnemyOnRouteMoveFailed)
    {
        return;
    }

    NotifyEnemyRemovedFromWave(Enemy, EWaveEnemyRemoveReason::Despawn);

    if (bDestroyEnemyOnRouteMoveFailed && IsValid(Enemy))
    {
        Enemy->Destroy();
    }
}

void AWaveSpawnManager::HandleTrackedEnemyDestroyed(AActor* DestroyedActor)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(DestroyedActor);
    if (!Enemy)
    {
        return;
    }

    const TWeakObjectPtr<ABaseEnemy> EnemyKey(Enemy);
    if (RemovedEnemies.Contains(EnemyKey))
    {
        return;
    }

    UE_LOG(LogWaveSpawnManager, Warning, TEXT("[WaveSpawnManager] Tracked enemy destroyed without explicit wave removal. Enemy=%s"), *GetNameSafe(Enemy));
    NotifyEnemyRemovedFromWave(Enemy, EWaveEnemyRemoveReason::Despawn);
}
