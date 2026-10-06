#include "WaveSystem/Manager/WaveSpawnManager.h"

#include "BaseEnemy.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
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

void AWaveSpawnManager::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomWaveState State;
	State.WaveDataPath = WaveData ? FSoftObjectPath(WaveData) : FSoftObjectPath();
	State.WaveArrayIndex = CurrentWaveArrayIndex;
	State.DisplayWaveNumber = CurrentDisplayWaveNumber;
	State.AliveEnemyCount = AliveEnemyCount;
	State.SpawnSerialCounter = SpawnSerialCounter;
	State.bWaveActive = bWaveActive;
	if (GetWorld())
	{
		State.bPreWaveTimerPending = GetWorldTimerManager().IsTimerActive(PreWaveDelayTimerHandle);
		if (State.bPreWaveTimerPending)
			State.PreWaveRemaining = FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(PreWaveDelayTimerHandle));
	}
	for (const FSpawnGroupRuntime& Group : RuntimeGroups)
	{
		FSWRoomWaveGroupState& Saved = State.Groups.AddDefaulted_GetRef();
		Saved.RemainingCount = Group.RemainingCount;
		Saved.SpawnedCount = Group.SpawnedCount;
		Saved.bFinished = Group.bFinished;
		Saved.State = Group.State;
		Saved.PendingTickets = Group.PendingTickets;
		if (GetWorld())
		{
			Saved.bTimerPending = GetWorldTimerManager().IsTimerActive(Group.TimerHandle);
			if (Saved.bTimerPending)
				Saved.TimerRemaining = FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(Group.TimerHandle));
			for (FSWRoomPendingSpawnTicket& Ticket : Saved.PendingTickets)
				Ticket.RemainingSeconds = Saved.TimerRemaining;
		}
	}
	for (const TWeakObjectPtr<ABaseEnemy>& EnemyPtr : ActiveEnemies)
	{
		const ABaseEnemy* Enemy = EnemyPtr.Get();
		const USWRoomSnapshotComponent* Id = Enemy ? Enemy->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr;
		if (Id && Id->StableId.IsValid()) State.ActiveEnemyIds.Add(Id->StableId);
		else
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Spawner");
			Issue.FieldKey = FName(*(TEXT("ActiveEnemy:") + GetPathNameSafe(Enemy)));
			Issue.Reason = FString::Printf(TEXT("Active enemy has no stable ID: %s"), *GetNameSafe(Enemy));
		}
	}
	State.ActiveEnemyIds.Sort([](const FGuid& A, const FGuid& B) { return A.ToString() < B.ToString(); });
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Spawner;
	Part.Version = 2;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner");
		Issue.FieldKey = TEXT("WaveState");
		Issue.Reason = TEXT("Wave adapter serialization failed");
	}
}

bool AWaveSpawnManager::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	if (Part.Domain != ESWRoomDomain::Spawner || Part.Version != 2)
	{
		OutError = FString::Printf(TEXT("Unsupported wave domain/version: Class=%s Domain=%d Version=%d Expected=2"),
			*GetClass()->GetPathName(), static_cast<int32>(Part.Domain), Part.Version);
		return false;
	}
	FSWRoomWaveState State;
	if (!FSWRoomStructCodec::Read(Part.Bytes, State) || State.AliveEnemyCount < 0
		|| State.SpawnSerialCounter < 0 || State.Groups.Num() > 100000
		|| !FMath::IsFinite(State.PreWaveRemaining) || State.PreWaveRemaining < 0.f)
	{
		OutError = TEXT("Invalid wave state");
		return false;
	}
	for (const FSWRoomWaveGroupState& Group : State.Groups)
		if (Group.RemainingCount < 0 || Group.SpawnedCount < 0
			|| !FMath::IsFinite(Group.TimerRemaining) || Group.TimerRemaining < 0.f)
		{
			OutError = TEXT("Invalid wave group state");
			return false;
		}
	for (int32 GroupIndex = 0; GroupIndex < State.Groups.Num(); ++GroupIndex)
		for (const FSWRoomPendingSpawnTicket& Ticket : State.Groups[GroupIndex].PendingTickets)
			if (Ticket.GroupIndex != GroupIndex || Ticket.Ordinal < State.Groups[GroupIndex].SpawnedCount
				|| Ticket.EnemyClass.IsNull() || !Ticket.ReservedId.IsValid()
				|| Ticket.WorldTransform.ContainsNaN() || !FMath::IsFinite(Ticket.RemainingSeconds)
				|| Ticket.RemainingSeconds < 0.f)
			{
				OutError = FString::Printf(TEXT("Invalid wave spawn ticket Class=%s Group=%d Ordinal=%d"),
					*GetClass()->GetPathName(), GroupIndex, Ticket.Ordinal);
				return false;
			}
	if (State.WaveDataPath != (WaveData ? FSoftObjectPath(WaveData) : FSoftObjectPath())
		|| (State.WaveArrayIndex != INDEX_NONE && (!WaveData || !WaveData->IsValidWaveIndex(State.WaveArrayIndex)))
		|| (State.WaveArrayIndex != INDEX_NONE
			&& State.Groups.Num() != WaveData->GetWaveDefinitionChecked(State.WaveArrayIndex).SpawnGroups.Num()))
	{
		UE_LOG(LogWaveSpawnManager, Warning, TEXT("Room wave definition changed; using new-level defaults Manager=%s"), *GetPathName());
		return true;
	}
	ClearAllSpawnTimers();
	CurrentWaveArrayIndex = State.WaveArrayIndex;
	CurrentDisplayWaveNumber = State.DisplayWaveNumber;
	AliveEnemyCount = State.AliveEnemyCount;
	SpawnSerialCounter = State.SpawnSerialCounter;
	bWaveActive = State.bWaveActive;
	CurrentWaveDefinition = State.WaveArrayIndex == INDEX_NONE ? FWaveDefinition()
		: WaveData->GetWaveDefinitionChecked(State.WaveArrayIndex);
	RuntimeGroups.SetNum(State.Groups.Num());
	for (int32 Index = 0; Index < State.Groups.Num(); ++Index)
	{
		FSpawnGroupRuntime& Group = RuntimeGroups[Index];
		const FSWRoomWaveGroupState& Saved = State.Groups[Index];
		Group.RemainingCount = Saved.RemainingCount;
		Group.SpawnedCount = Saved.SpawnedCount;
		Group.bFinished = Saved.bFinished;
		Group.State = Saved.State;
		Group.PendingTickets = Saved.PendingTickets;
		Group.TimerHandle.Invalidate();
	}
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AWaveSpawnManager::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	if (bAutoBuildRouteMapOnBeginPlay) BuildRouteMap();
	if (bBindToWaveGameMode) BindToWaveGameMode();
	ActiveEnemies.Reset();
	RemovedEnemies.Reset();
	for (const FGuid& Id : PendingRoomState.ActiveEnemyIds)
		if (AActor* const* Found = RegisteredActors.Find(Id))
			if (ABaseEnemy* Enemy = Cast<ABaseEnemy>(*Found))
			{
				ActiveEnemies.Add(Enemy);
				BindEnemyDelegates(Enemy, ResolveWaypointMoveComponent(Enemy));
			}
	if (PendingRoomState.bPreWaveTimerPending)
	{
		const float Delay = FMath::Max(PendingRoomState.PreWaveRemaining, KINDA_SMALL_NUMBER);
		GetWorldTimerManager().SetTimer(PreWaveDelayTimerHandle, this, &AWaveSpawnManager::BeginWaveSpawning, Delay, false);
	}
	for (int32 Index = 0; Index < PendingRoomState.Groups.Num(); ++Index)
	{
		const FSWRoomWaveGroupState& Saved = PendingRoomState.Groups[Index];
		if (!Saved.bTimerPending || !RuntimeGroups.IsValidIndex(Index)) continue;
		FTimerDelegate Callback;
		if (Saved.State == EWaveSpawnGroupState::Waiting)
			Callback.BindUObject(this, &AWaveSpawnManager::BeginSpawnGroup, Index);
		else if (Saved.State == EWaveSpawnGroupState::Spawning)
			Callback.BindUObject(this, &AWaveSpawnManager::SpawnBurstForGroup, Index);
		else continue;
		GetWorldTimerManager().SetTimer(RuntimeGroups[Index].TimerHandle, Callback,
			FMath::Max(Saved.TimerRemaining, KINDA_SMALL_NUMBER), false);
	}
	PendingRoomState = FSWRoomWaveState();
	return true;
}

AWaveSpawnManager::AWaveSpawnManager()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
}

void AWaveSpawnManager::BeginPlay()
{
    Super::BeginPlay();
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;

    if (!HasAuthority())
    {
        return;
    }
	if (const USWRoomSnapshotSubsystem* Snapshot = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
		Snapshot && Snapshot->IsRestoringSnapshot()) return;

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
	if (USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(this)) return false;
    if (!HasAuthority())
    {
        return false;
    }
	if (const USWRoomSnapshotSubsystem* Snapshot = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
		Snapshot && Snapshot->IsRestoringSnapshot()) return false;

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
	if (USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(this)) return;
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
	if (USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(this)) return;
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

    PrepareSpawnTickets(SpawnGroupIndex, 0.f);
    SpawnBurstForGroup(SpawnGroupIndex);
}

bool AWaveSpawnManager::PrepareSpawnTickets(int32 SpawnGroupIndex, float DelaySeconds)
{
	if (!RuntimeGroups.IsValidIndex(SpawnGroupIndex)
		|| !CurrentWaveDefinition.SpawnGroups.IsValidIndex(SpawnGroupIndex)) return false;
	FSpawnGroupRuntime& Group = RuntimeGroups[SpawnGroupIndex];
	if (!Group.PendingTickets.IsEmpty()) return true;
	const FSpawnGroupDefinition& Definition = CurrentWaveDefinition.SpawnGroups[SpawnGroupIndex];
	ASpawnRoute* Route = FindRouteById(Definition.RouteId);
	if (!IsValid(Route) || !Definition.EnemyClass) return false;
	const int32 Count = FMath::Min(Group.RemainingCount, FMath::Max(1, Definition.BurstCount));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FSWRoomPendingSpawnTicket& Ticket = Group.PendingTickets.AddDefaulted_GetRef();
		Ticket.GroupIndex = SpawnGroupIndex;
		Ticket.Ordinal = Group.SpawnedCount + Index;
		Ticket.EnemyClass = FSoftClassPath(Definition.EnemyClass.Get());
		Ticket.StatsTable = FSoftObjectPath(Definition.StatsRow.DataTable);
		Ticket.StatsRow = Definition.StatsRow.RowName;
		Ticket.WorldTransform = Route->GetSpawnTransform(Definition.SpawnRadius);
		Ticket.ReservedId = FGuid::NewGuid();
		Ticket.RemainingSeconds = DelaySeconds;
	}
	return true;
}

void AWaveSpawnManager::SpawnBurstForGroup(int32 SpawnGroupIndex)
{
	if (USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(this)) return;
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
	bool bRetryUnconsumed = false;

    do
    {
		if (!PrepareSpawnTickets(SpawnGroupIndex, 0.f)) break;
        const int32 BurstCount = FMath::Max(1, GroupDefinition.BurstCount);
        int32 SpawnedThisBurst = 0;

        while (RuntimeGroup.RemainingCount > 0 && SpawnedThisBurst < BurstCount)
        {
			if (RuntimeGroup.PendingTickets.IsEmpty()) break;
			const FSWRoomPendingSpawnTicket Ticket = RuntimeGroup.PendingTickets[0];
            const bool bSpawned = SpawnOneEnemyFromGroup(
                SpawnGroupIndex,
                GroupDefinition,
                Ticket
            );

            if (bSpawned || bConsumeSpawnCountOnSpawnFailure)
            {
                --RuntimeGroup.RemainingCount;
                ++RuntimeGroup.SpawnedCount;
				RuntimeGroup.PendingTickets.RemoveAt(0);
            }
            else
            {
				bRetryUnconsumed = true;
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

        if (!bSpawnAllImmediately || bRetryUnconsumed)
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
	const float NextDelay = bSpawnAllImmediately ? 0.05f : GroupDefinition.SpawnInterval;
	PrepareSpawnTickets(SpawnGroupIndex, NextDelay);
    GetWorldTimerManager().SetTimer(
        RuntimeGroup.TimerHandle,
        TimerDelegate,
        NextDelay,
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
    return FMath::Max(0.f, CurrentWaveDefinition.WaveTimeLimit);
}

bool AWaveSpawnManager::SpawnOneEnemyFromGroup(int32 SpawnGroupIndex, const FSpawnGroupDefinition& SpawnGroupDefinition,
	const FSWRoomPendingSpawnTicket& Ticket)
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

    const FTransform SpawnTransform = Ticket.WorldTransform;
	UClass* SavedEnemyClass = Ticket.EnemyClass.TryLoadClass<ABaseEnemy>();
	if (!SavedEnemyClass || !Ticket.ReservedId.IsValid()) return false;

	const USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
    ABaseEnemy* SpawnedEnemy = FSWVoyageSpawn::SpawnDeferred<ABaseEnemy>(World,
        SavedEnemyClass,
        SpawnTransform,
        this, nullptr, SpawnCollisionHandlingMethod, ESWVoyageActorLifetime::Voyage, Voyage ? Voyage->GetGeneration() : 0
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

	FDataTableRowHandle SavedStatsRow;
	SavedStatsRow.DataTable = Cast<UDataTable>(Ticket.StatsTable.TryLoad());
	SavedStatsRow.RowName = Ticket.StatsRow;
    if (!SpawnedEnemy->ConfigureSpawnBalance(SavedStatsRow,
        SpawnGroupDefinition.HealthMultiplier, SpawnGroupDefinition.SpeedMultiplier))
    {
        SpawnedEnemy->Destroy();
        return false;
    }
	if (USWRoomSnapshotComponent* Snapshot = SpawnedEnemy->FindComponentByClass<USWRoomSnapshotComponent>())
	{
		const FGuid PreviousId = Snapshot->StableId;
		Snapshot->SetRuntimeId(Ticket.ReservedId);
		if (USWRoomSnapshotSubsystem* Subsystem = World->GetSubsystem<USWRoomSnapshotSubsystem>())
			Subsystem->UpdateRegisteredActorId(SpawnedEnemy, PreviousId, Ticket.ReservedId);
	}
    if (USWVoyageSpawnLibrary::FinishVoyageActorSpawn(SpawnedEnemy, SpawnTransform) != SpawnedEnemy
		|| !IsValid(SpawnedEnemy) || !SpawnedEnemy->IsBalanceReady())
    {
        if (IsValid(SpawnedEnemy)) SpawnedEnemy->Destroy();
        return false;
    }
    SpawnedEnemy->SpawnDefaultController();

    const int32 EnemySeed = GenerateEnemyRouteSeed(CurrentWaveArrayIndex, SpawnGroupIndex, Ticket.Ordinal);
    UEnemyWaypointMoveComponent* WaypointMoveComponent = ResolveWaypointMoveComponent(SpawnedEnemy);

    const TWeakObjectPtr<ABaseEnemy> EnemyKey(SpawnedEnemy);
    ActiveEnemies.Add(EnemyKey);
    RemovedEnemies.Remove(EnemyKey);
    ++AliveEnemyCount;

    BindEnemyDelegates(SpawnedEnemy, WaypointMoveComponent);
    

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

FName AWaveSpawnManager::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<AWaveSpawnManager*>(this)) : NAME_None;
}

ESWVoyageStepResult AWaveSpawnManager::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	auto Pause = [this](FTimerHandle Handle)
	{
		if (!PausedVoyageTimers.Contains(Handle) && GetWorldTimerManager().IsTimerActive(Handle))
		{
			GetWorldTimerManager().PauseTimer(Handle); PausedVoyageTimers.Add(Handle);
		}
	};
	Pause(PreWaveDelayTimerHandle);
	for (const FSpawnGroupRuntime& Group : RuntimeGroups) Pause(Group.TimerHandle);
	return ESWVoyageStepResult::Succeeded;
}

void AWaveSpawnManager::CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context)
{
	for (FTimerHandle Handle : PausedVoyageTimers) GetWorldTimerManager().UnPauseTimer(Handle);
	PausedVoyageTimers.Reset();
}

ESWVoyageStepResult AWaveSpawnManager::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	UnbindFromWaveGameMode(); StopActiveWave(EWaveEndReason::TimeExpired, true, false); ClearAllSpawnTimers();
	RouteMap.Reset(); RuntimeGroups.Reset(); ActiveEnemies.Reset(); RemovedEnemies.Reset();
	PausedVoyageTimers.Reset(); RestoredVoyageGeneration = ResumedVoyageGeneration = INDEX_NONE;
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult AWaveSpawnManager::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!Context.bAuthority) return ESWVoyageStepResult::Succeeded;
	const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{
		OutError = TEXT("WaveSpawnerGenerationMismatch"); return ESWVoyageStepResult::Failed;
	}
	if (RestoredVoyageGeneration != Context.Generation)
	{
		if (bAutoBuildRouteMapOnBeginPlay) BuildRouteMap();
		if (bValidateOnBeginPlay && !ValidateManagerSetup())
		{
			OutError = TEXT("WaveSpawnerRequiredConfigurationInvalid"); return ESWVoyageStepResult::Failed;
		}
		RestoredVoyageGeneration = Context.Generation;
	}
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult AWaveSpawnManager::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	for (const TWeakObjectPtr<ABaseEnemy>& Enemy : ActiveEnemies)
		if (!Enemy.IsValid() || !USWVoyageSpawnLibrary::IsActorFromCurrentVoyage(Enemy.Get()))
		{
			OutError = TEXT("WaveSpawnerActiveEnemyGenerationInvalid"); return ESWVoyageStepResult::Failed;
		}
	return ESWVoyageStepResult::Succeeded;
}

void AWaveSpawnManager::ResumeVoyage_Implementation(const FSWVoyageResetContext& Context)
{
	CancelVoyagePreparation_Implementation(Context);
	if (!Context.bAuthority || ResumedVoyageGeneration == Context.Generation) return;
	ResumedVoyageGeneration = Context.Generation;
	if (bBindToWaveGameMode) BindToWaveGameMode();
	if (!Context.bContinue) ReportWaveDataReady();
}
