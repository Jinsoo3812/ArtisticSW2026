#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "ShipAI/EnemyShipDiagnostics.h"
#include "HAL/PlatformTime.h"
#include "Perception/AISenseConfig_Sight.h"

#include "AI/BaseAIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "ShipAI/NavalAIController.h"
#include "Misc/ScopeExit.h"

UDeckEnemySpawnerComponent::UDeckEnemySpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UDeckEnemySpawnerComponent::BeginPlay()
{
	Super::BeginPlay();
	SpawnStartReadyTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, SpawnStartDelay);
	if (AEnemyShip* Host = GetHostShip(); Host && Host->HasAuthority())
	{
		HostStateHandle = Host->OnRuntimeStateChanged.AddUObject(this, &UDeckEnemySpawnerComponent::HandleHostRuntimeStateChanged);
		if (auto* Area = Host->GetDeckWalkAreaComponent())
			WalkAreaHandle = Area->OnReadinessChanged.AddUObject(this, &UDeckEnemySpawnerComponent::HandleWalkAreaReadinessChanged);
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
			RestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &UDeckEnemySpawnerComponent::HandleRoomRestoreCompleted);
		RequestReadinessEvaluation(); // Catch up even when the owner changed state before binding.
	}
}

void UDeckEnemySpawnerComponent::UnbindLifecycleDelegates()
{
	if (AEnemyShip* Host = GetHostShip())
	{
		Host->OnRuntimeStateChanged.Remove(HostStateHandle);
		if (auto* Area = Host->GetDeckWalkAreaComponent()) Area->OnReadinessChanged.Remove(WalkAreaHandle);
	}
	if (GetWorld())
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->OnRestoreCompleted.Remove(RestoreCompletedHandle);
}

void UDeckEnemySpawnerComponent::HandleHostRuntimeStateChanged(const FEnemyShipRuntimeState&, const FEnemyShipRuntimeState& Current)
{
	if (Current.Phase != EEnemyShipRuntimePhase::Active) PauseDeployment(TEXT("HostInactive"));
	RequestReadinessEvaluation();
}

void UDeckEnemySpawnerComponent::HandleWalkAreaReadinessChanged(bool bReady, int32)
{
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	if (!bReady) PauseDeployment(TEXT("WalkAreaNotReady"));
	RequestReadinessEvaluation();
}

void UDeckEnemySpawnerComponent::HandleRoomRestoreCompleted()
{
	RequestReadinessEvaluation();
}

void UDeckEnemySpawnerComponent::SetWaitReason(FName Reason)
{
	if (LastSpawnReason == Reason) return;
	LastSpawnReason = Reason;
	/* EmitCrewDiagnostic(TEXT("Readiness"), TEXT("Reason=") + Reason.ToString()); */ {}
}

void UDeckEnemySpawnerComponent::EmitCrewDiagnostic(const TCHAR* Event, const FString& Details, const AShip* ObservedShip)
{
	const auto* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !EnemyShipDiagnostics::AdmitCrewEvent(CrewLogWindow, CrewLogCount, SuppressedCrewLogs)) return;
	const auto* Identity = Host->FindComponentByClass<USWRoomSnapshotComponent>();
	const auto* Area = Host->GetDeckWalkAreaComponent();
	const auto* NavalController = Cast<ANavalAIController>(Host->GetController());
	const AShip* Sighted = NavalController ? NavalController->FindSightedPlayerShip(DeploymentTriggerShip.Get()) : nullptr;
	const AShip* Target = ObservedShip ? ObservedShip : DeploymentTriggerShip.Get();
	const float Distance = Target ? FVector::Dist2D(Host->GetActorLocation(), Target->GetActorLocation()) : -1.f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.;
	const int32 PointId = DeploymentQueue.IsValidIndex(DeploymentQueueIndex) ? DeploymentQueue[DeploymentQueueIndex].SpawnPointId : INDEX_NONE;
	/* Diagnostics muted after pooling/crew verification.
	UE_LOG(LogEnemyCrew, Display, TEXT("Event=%s Host=%s Placement=%s HostId=%s Generation=%u Request=%u Epoch=%u RequestState=%d DeploymentState=%d Enabled=%d Pool=%d Alive=%d Plan=%d Slot=%d Point=%d HostActive=%d PoolIdle=%d DistanceDormant=%d StoryDormant=%d CrewDefeated=%d WalkReady=%d WalkRevision=%d Target=%s Sighted=%s DistanceCM=%.1f SightRadiusCM=%.1f SpawnDelayLeft=%.3f SightDeadlineLeft=%.3f ReactionLeft=%.3f Suppressed=%u %s"),
		Event, *GetNameSafe(Host), *Host->GetBundleAuthoredName().ToString(), Identity ? *Identity->StableId.ToString() : TEXT("None"),
		EncounterGeneration, RequestId, ExecutionEpoch, static_cast<int32>(SpawnRequestState), static_cast<int32>(DeploymentState), IsEnabled(),
		EnemyPool.Num(), AliveDeployedEnemies.Num(), SpawnPlan.Num(), DeploymentQueueIndex, PointId,
		Host->CanDeployDeckEnemies(), Host->IsBundlePoolIdle(), Host->IsDistanceOptimizationDormant(), Host->IsStoryGateDormant(), Host->IsCrewDefeated(),
		Area && Area->IsReady(), Area ? Area->GetRevision() : INDEX_NONE, *GetNameSafe(Target), *GetNameSafe(Sighted), Distance,
		NavalController ? NavalController->GetConfiguredSightRadius() : -1.f,
		GetRemainingSpawnStartDelay(), PendingSightExpiry > 0. ? PendingSightExpiry - Now : -1., ReactionReadyTime > 0. ? ReactionReadyTime - Now : -1.,
		SuppressedCrewLogs, *Details); */ {}
	SuppressedCrewLogs = 0;
}

void UDeckEnemySpawnerComponent::ReportSightObservation(AShip* PlayerShip, bool bSensed, float StimulusAge)
{
	if (!EnemyShipDiagnostics::CrewEnabled()) return;
	/* EmitCrewDiagnostic(bSensed ? TEXT("SightAcquired") : TEXT("SightLost"), FString::Printf(TEXT("StimulusAge=%.3f"), StimulusAge), PlayerShip); */ {}
}

FString UDeckEnemySpawnerComponent::GetPlanSignature() const
{
	FString Result;
	for (const auto& Slot : SpawnPlan)
	{
		const auto* CDO = Slot.EnemyClass ? Slot.EnemyClass->GetDefaultObject<ADeckEnemy>() : nullptr;
		const auto& Row = CDO && Slot.StatsRow.IsNull() ? CDO->DefaultStatsRow : Slot.StatsRow;
		Result += FString::Printf(TEXT("%s|%s|%s|%d;"), *GetPathNameSafe(Slot.EnemyClass.Get()),
			*GetPathNameSafe(Row.DataTable), *Row.RowName.ToString(), Slot.SpawnPointId);
	}
	return Result;
}

bool UDeckEnemySpawnerComponent::CanSuspendForDistanceOptimization() const
{
	// The ship's Idle/home/distance policy owns retirement. Crew state must not
	// keep a remote Idle hull allocated. Only defer an in-flight activation transaction.
	return !bActivationInProgress;
}

void UDeckEnemySpawnerComponent::RequestReadinessEvaluation()
{
	if (bShuttingDown || !GetWorld() || !GetHostShip() || !GetHostShip()->HasAuthority() || bReadinessEvaluationQueued) return;
	GetWorld()->GetTimerManager().ClearTimer(ReadinessTimerHandle);
	bReadinessEvaluationQueued = true;
	const uint32 Epoch = ExecutionEpoch, Generation = EncounterGeneration;
	ReadinessTimerHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Epoch, Generation]
	{
		if (Epoch != ExecutionEpoch || Generation != EncounterGeneration || bShuttingDown) return;
		bReadinessEvaluationQueued = false;
		EvaluateDeploymentReadiness();
	}));
}

void UDeckEnemySpawnerComponent::PauseDeployment(FName Reason)
{
	if (SpawnRequestState != EDeckEnemySpawnRequestState::PendingReadiness && SpawnRequestState != EDeckEnemySpawnRequestState::Running) return;
	if (!bDeploymentPaused)
	{
		PausedDeploymentDelay = FMath::Max(0.f, GetWorld()->GetTimerManager().GetTimerRemaining(
			DeploymentState == EDeckEnemyDeploymentState::Preparing ? SightDelayTimerHandle : DeploymentTimerHandle));
		GetWorld()->GetTimerManager().ClearTimer(SightDelayTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(DeploymentTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(ReadinessTimerHandle);
		bReadinessEvaluationQueued = false;
		++ExecutionEpoch;
		bDeploymentPaused = true;
		DeploymentTicket = FSWRoomDeckDeploymentTicket();
		if (SpawnRequestState == EDeckEnemySpawnRequestState::Running)
			ReadinessExpiry = GetWorld()->GetTimeSeconds() + FMath::Max(1.f, ReadinessTimeout);
	}
	SetWaitReason(Reason);
}

void UDeckEnemySpawnerComponent::ScheduleDeployment(float Delay, bool bBegin)
{
	// Pooling fix (2026-10-13): preserve epoch/generation checks across retirement and save restore.
	// Never ClearTimer(Handle) inside this callback: it can destroy the delegate currently executing.
	// Direct readiness evaluation also stalls because IsTimerActive includes the Executing timer.
	// Queue a separate evaluation after the one-shot callback returns instead.
	FTimerHandle& Handle = bBegin ? SightDelayTimerHandle : DeploymentTimerHandle;
	const uint32 Epoch = ExecutionEpoch, Generation = EncounterGeneration;
	CreateDeploymentTicket(Delay);
	GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Epoch, Generation]
	{
		if (Epoch != ExecutionEpoch || Generation != EncounterGeneration || bShuttingDown) return;
		// Executing one-shot timers are still active. Recheck after this timer has expired.
		RequestReadinessEvaluation();
	}), FMath::Max(0.001f, Delay), false);
}

void UDeckEnemySpawnerComponent::EvaluateDeploymentReadiness()
{
	if (bShuttingDown || bEvaluatingReadiness || bActivationInProgress) { if (!bShuttingDown) RequestReadinessEvaluation(); return; }
	TGuardValue<bool> Evaluating(bEvaluatingReadiness, true);
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority()) return;
	if (Host->IsDeathHandled() || Host->IsSinking() || Host->IsCrewDefeated() || Host->IsStoryGateDormant())
	{
		SetWaitReason(Host->IsStoryGateDormant() ? TEXT("StoryGateClosed") : TEXT("HostTerminal"));
		CancelDeployment();
		return;
	}
	const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	if (bHasPendingRoomState || (Room && Room->IsRestoringSnapshot())) { PauseDeployment(TEXT("SnapshotRestoring")); return; }
	const auto* Controller = Cast<ANavalAIController>(Host->GetController());
	AShip* Sighted = Controller ? Controller->FindSightedPlayerShip(DeploymentTriggerShip.Get()) : nullptr;
	if (SpawnRequestState == EDeckEnemySpawnRequestState::None || SpawnRequestState == EDeckEnemySpawnRequestState::Cancelled)
	{
		if (Host->CanDeployDeckEnemies() && Sighted) RequestDeployment(Sighted);
		return;
	}
	if (SpawnRequestState == EDeckEnemySpawnRequestState::Finished) return;
	const double Now = GetWorld()->GetTimeSeconds();
	auto Wait = [this](FName Reason, double Deadline)
	{
		PauseDeployment(Reason);
		const uint32 Epoch = ExecutionEpoch, Generation = EncounterGeneration;
		GetWorld()->GetTimerManager().SetTimer(ReadinessTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, Epoch, Generation]
		{
			if (Epoch == ExecutionEpoch && Generation == EncounterGeneration && !bShuttingDown) EvaluateDeploymentReadiness();
		}), FMath::Max(0.01f, static_cast<float>(Deadline - GetWorld()->GetTimeSeconds())), false);
	};
	if (SpawnRequestState == EDeckEnemySpawnRequestState::PendingReadiness && !Sighted)
	{
		if (Now >= PendingSightExpiry) { SetWaitReason(TEXT("SightExpired")); CancelDeployment(); }
		else Wait(TEXT("WaitingForSight"), PendingSightExpiry);
		return;
	}
	if (Sighted) DeploymentTriggerShip = Sighted;
	if (!DeploymentTriggerShip.IsValid() || DeploymentTriggerShip->IsSinking())
	{
		if (TargetWaitExpiry <= 0.) TargetWaitExpiry = Now + FMath::Max(0.1f, PendingSightLifetime);
		if (Now >= TargetWaitExpiry) { SetWaitReason(TEXT("TargetExpired")); CancelDeployment(); }
		else Wait(TEXT("WaitingForTarget"), TargetWaitExpiry);
		return;
	}
	TargetWaitExpiry = 0.;
	const auto* Area = Host->GetDeckWalkAreaComponent();
	if (!Host->CanDeployDeckEnemies() || !Area || !Area->IsReady())
	{
		if (ReadinessExpiry > 0. && Now >= ReadinessExpiry)
		{
			SetWaitReason(TEXT("ReadinessTimeout"));
			CancelDeployment();
			SpawnRequestState = EDeckEnemySpawnRequestState::Finished;
			return;
		}
		Wait(Host->IsDistanceOptimizationDormant() ? TEXT("DistanceDormant") : TEXT("WalkAreaNotReady"), ReadinessExpiry);
		return;
	}
	if (EnemyPool.IsEmpty()) InitializePool();
	if (EnemyPool.IsEmpty()) { SetWaitReason(TEXT("PoolAllocationFailed")); DeploymentState = EDeckEnemyDeploymentState::Failed; SpawnRequestState = EDeckEnemySpawnRequestState::Finished; return; }
	if (bDeploymentPaused)
	{
		bDeploymentPaused = false;
		if (PausedDeploymentDelay > 0.f)
		{
			const float Delay = PausedDeploymentDelay;
			PausedDeploymentDelay = 0.f;
			ScheduleDeployment(Delay, DeploymentState == EDeckEnemyDeploymentState::Preparing);
			return;
		}
	}
	if (GetWorld()->GetTimerManager().IsTimerActive(SightDelayTimerHandle)
		|| GetWorld()->GetTimerManager().IsTimerActive(DeploymentTimerHandle)) return;
	const float Delay = FMath::Max(GetRemainingSpawnStartDelay(), static_cast<float>(FMath::Max(0., ReactionReadyTime - Now)));
	if (DeploymentState == EDeckEnemyDeploymentState::Preparing && Delay > 0.f)
	{
		SetWaitReason(TEXT("SpawnDelay")); ScheduleDeployment(Delay, true); return;
	}
	SetWaitReason(NAME_None);
	if (DeploymentState == EDeckEnemyDeploymentState::Preparing) BeginDeployment();
	else if (DeploymentState == EDeckEnemyDeploymentState::Deploying) DeployNextEnemy();
}

float UDeckEnemySpawnerComponent::GetRemainingSpawnStartDelay() const
{
	return GetWorld() ? static_cast<float>(FMath::Max(0.0,
		SpawnStartReadyTime - GetWorld()->GetTimeSeconds())) : 0.f;
}

void UDeckEnemySpawnerComponent::CaptureRoomState(FSWRoomDeckSpawnerState& OutState, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	OutState = FSWRoomDeckSpawnerState();
	OutState.LifecycleVersion = 1;
	OutState.RequestState = static_cast<uint8>(SpawnRequestState);
	OutState.EncounterGeneration = EncounterGeneration;
	OutState.RequestId = RequestId;
	OutState.PlanSignature = GetPlanSignature();
	OutState.SlotResults = SlotResults;
	OutState.SlotEnemyIds = SlotEnemyIds;
	if (OutState.SlotResults.IsEmpty()) OutState.SlotResults.Init(0, SpawnPlan.Num());
	OutState.SlotEnemyIds.SetNum(SpawnPlan.Num());
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.;
	OutState.ReactionDelayRemaining = FMath::Max(0., ReactionReadyTime - Now);
	OutState.PendingSightRemaining = FMath::Max(0., PendingSightExpiry - Now);
	OutState.ReadinessTimeoutRemaining = FMath::Max(0., ReadinessExpiry - Now);
	OutState.TargetWaitRemaining = FMath::Max(0., TargetWaitExpiry - Now);
	if (bActivationInProgress)
	{
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner"); Issue.FieldKey = TEXT("ActivationTransaction");
		Issue.Reason = TEXT("Cannot capture inside an uncommitted activation");
	}
	OutState.PlanCount = SpawnPlan.Num();
	OutState.DeploymentState = static_cast<uint8>(DeploymentState);
	OutState.bAllDeployedEnemiesDefeated = bAllDeployedEnemiesDefeated;
	OutState.bHasDeployedEnemy = bHasDeployedEnemy;
	OutState.NextReservationSerial = NextReservationSerial;
	OutState.ActivationSerial = ActivationSerial;
	OutState.DeploymentQueueIndex = DeploymentQueueIndex;
	OutState.CurrentRetryCount = CurrentRetryCount;
	OutState.SpawnStartDelayRemaining = GetRemainingSpawnStartDelay();
	OutState.DeploymentFailureCount = DeploymentFailureCount;
	if (GetWorld())
	{
		OutState.SightDelayRemaining = FMath::Max(0.f, GetWorld()->GetTimerManager().GetTimerRemaining(SightDelayTimerHandle));
		OutState.DeploymentTimerRemaining = FMath::Max(0.f, GetWorld()->GetTimerManager().GetTimerRemaining(DeploymentTimerHandle));
	}
	auto GetId = [](const AActor* Actor) -> FGuid
	{
		if (const USWRoomSnapshotComponent* Id = Actor ? Actor->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr)
			return Id->StableId;
		return FGuid();
	};
	OutState.TriggerShipId = GetId(DeploymentTriggerShip.Get());
	OutState.InitialTargetId = GetId(DeploymentInitialTarget.Get());
	if (bDeploymentPaused)
	{
		if (DeploymentState == EDeckEnemyDeploymentState::Preparing) OutState.SightDelayRemaining = PausedDeploymentDelay;
		else OutState.DeploymentTimerRemaining = PausedDeploymentDelay;
	}
	for (ADeckEnemy* Enemy : EnemyPool)
		if (IsValid(Enemy))
		{
			const FGuid Id = GetId(Enemy);
			if (Id.IsValid()) OutState.EnemyPoolIds.Add(Id);
			else
			{
				FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Domain = TEXT("Spawner");
				Issue.FieldKey = FName(*(TEXT("PoolEnemy:") + Enemy->GetPathName()));
				Issue.Reason = TEXT("Deck pool enemy lacks stable ID");
			}
		}
	for (const TWeakObjectPtr<ADeckEnemy>& Enemy : AliveDeployedEnemies)
		if (Enemy.IsValid() && GetId(Enemy.Get()).IsValid()) OutState.AliveDeployedEnemyIds.Add(GetId(Enemy.Get()));
	for (const TPair<int32, FDeckPointRuntimeState>& Pair : PointRuntimeStates)
	{
		FSWRoomDeckPointState& Point = OutState.Points.AddDefaulted_GetRef();
		Point.PointId = Pair.Key;
		Point.OccupantId = GetId(Pair.Value.Occupant.Get());
		Point.ReservedById = GetId(Pair.Value.ReservedBy.Get());
		Point.ReservationSerial = Pair.Value.ReservationSerial;
	}
	OutState.EnemyPoolIds.Sort();
	OutState.AliveDeployedEnemyIds.Sort();
	OutState.Points.Sort([](const FSWRoomDeckPointState& A, const FSWRoomDeckPointState& B) { return A.PointId < B.PointId; });
	if (DeploymentState == EDeckEnemyDeploymentState::Preparing || DeploymentState == EDeckEnemyDeploymentState::Deploying)
	{
		OutState.DeploymentTicket = DeploymentTicket;
		OutState.DeploymentTicket.RemainingSeconds = DeploymentState == EDeckEnemyDeploymentState::Preparing
			? OutState.SightDelayRemaining : OutState.DeploymentTimerRemaining;
	}
}


bool UDeckEnemySpawnerComponent::RestoreRoomState(const FSWRoomDeckSpawnerState& Saved, FString& OutError)
{
	FSWRoomDeckSpawnerState State = Saved;
	if (State.PlanCount != SpawnPlan.Num() || State.DeploymentState > static_cast<uint8>(EDeckEnemyDeploymentState::Failed)
		|| State.NextReservationSerial == 0 || State.ActivationSerial < 0 || State.DeploymentQueueIndex < 0
		|| State.DeploymentQueueIndex > State.PlanCount || State.CurrentRetryCount < 0 || State.DeploymentFailureCount < 0
		|| State.DeploymentFailureCount > State.DeploymentQueueIndex || State.LifecycleVersion > 1)
	{
		OutError = TEXT("Invalid deck deployment state or changed plan"); return false;
	}
	for (float Remaining : {State.SightDelayRemaining, State.SpawnStartDelayRemaining, State.DeploymentTimerRemaining,
		State.ReactionDelayRemaining, State.PendingSightRemaining, State.ReadinessTimeoutRemaining, State.TargetWaitRemaining})
		if (!FMath::IsFinite(Remaining) || Remaining < 0.f) { OutError = TEXT("Invalid deck deployment deadline"); return false; }
	if (State.DeploymentTicket.bValid && (!State.DeploymentTicket.PoolActorId.IsValid()
		|| State.DeploymentTicket.QueueIndex != State.DeploymentQueueIndex || State.DeploymentTicket.WorldTransform.ContainsNaN()))
	{
		OutError = TEXT("Invalid deck deployment ticket"); return false;
	}
	if (State.LifecycleVersion == 0)
	{
		// Old saves have no per-slot results. Only all-success/all-failure consumed prefixes are unambiguous.
		if (State.DeploymentFailureCount > 0 && State.DeploymentFailureCount != State.DeploymentQueueIndex)
		{ OutError = TEXT("Legacy partial deck deployment has ambiguous slot results; cannot safely resume"); return false; }
		State.SlotResults.Init(0, State.PlanCount); State.SlotEnemyIds.SetNum(State.PlanCount);
		for (int32 Index = 0; Index < State.DeploymentQueueIndex; ++Index) State.SlotResults[Index] = State.DeploymentFailureCount ? 2 : 1;
		const auto OldPhase = static_cast<EDeckEnemyDeploymentState>(State.DeploymentState);
		State.RequestState = static_cast<uint8>(OldPhase == EDeckEnemyDeploymentState::Preparing ? EDeckEnemySpawnRequestState::PendingReadiness
			: OldPhase == EDeckEnemyDeploymentState::Deploying ? EDeckEnemySpawnRequestState::Running
			: OldPhase == EDeckEnemyDeploymentState::Idle ? EDeckEnemySpawnRequestState::None : EDeckEnemySpawnRequestState::Finished);
		State.ReactionDelayRemaining = State.SightDelayRemaining;
		State.PendingSightRemaining = FMath::Max(PendingSightLifetime, State.SightDelayRemaining);
		State.ReadinessTimeoutRemaining = FMath::Max(ReadinessTimeout, State.SightDelayRemaining + 1.f);
	}
	else if (State.PlanSignature != GetPlanSignature()) { OutError = TEXT("Deck deployment plan signature changed"); return false; }
	if (State.RequestState > static_cast<uint8>(EDeckEnemySpawnRequestState::Cancelled) || State.EncounterGeneration == 0
		|| State.SlotResults.Num() != State.PlanCount || State.SlotEnemyIds.Num() != State.PlanCount)
	{ OutError = TEXT("Invalid deck request or slot ledger"); return false; }
	int32 FailedCount = 0;
	for (int32 Index = 0; Index < State.SlotResults.Num(); ++Index)
	{
		if (State.SlotResults[Index] > 2 || (Index < State.DeploymentQueueIndex && State.SlotResults[Index] == 0))
		{ OutError = TEXT("Invalid deck slot progress"); return false; }
		FailedCount += State.SlotResults[Index] == 2 ? 1 : 0;
	}
	if (FailedCount != State.DeploymentFailureCount) { OutError = TEXT("Deck failure count disagrees with slot ledger"); return false; }
	CancelDeployment(); // Also invalidates next-tick callbacks and old execution tickets.
	const double Now = GetWorld()->GetTimeSeconds();
	SpawnStartReadyTime = Now + State.SpawnStartDelayRemaining;
	ReactionReadyTime = Now + State.ReactionDelayRemaining;
	PendingSightExpiry = Now + State.PendingSightRemaining;
	ReadinessExpiry = Now + State.ReadinessTimeoutRemaining;
	TargetWaitExpiry = State.TargetWaitRemaining > 0.f ? Now + State.TargetWaitRemaining : 0.;
	EnemyPool.Reset(); AliveDeployedEnemies.Reset(); PointRuntimeStates.Reset(); DeploymentQueue.Reset();
	DeploymentState = static_cast<EDeckEnemyDeploymentState>(State.DeploymentState);
	SpawnRequestState = static_cast<EDeckEnemySpawnRequestState>(State.RequestState);
	EncounterGeneration = State.EncounterGeneration; RequestId = State.RequestId;
	SlotResults = State.SlotResults; SlotEnemyIds = State.SlotEnemyIds;
	bAllDeployedEnemiesDefeated = State.bAllDeployedEnemiesDefeated; bHasDeployedEnemy = State.bHasDeployedEnemy;
	NextReservationSerial = State.NextReservationSerial; ActivationSerial = State.ActivationSerial;
	DeploymentQueueIndex = State.DeploymentQueueIndex; CurrentRetryCount = State.CurrentRetryCount;
	DeploymentFailureCount = State.DeploymentFailureCount;
	DeploymentTicket = State.DeploymentTicket; // Preserve the capture contract; re-resolve before actual activation.
	if ((SpawnRequestState == EDeckEnemySpawnRequestState::PendingReadiness || SpawnRequestState == EDeckEnemySpawnRequestState::Running)
		&& !BuildDeploymentPlan(DeploymentQueue, false))
	{ OutError = TEXT("Deck deployment plan cannot be reconstructed"); return false; }
	bDeploymentPaused = true;
	PausedDeploymentDelay = DeploymentState == EDeckEnemyDeploymentState::Preparing ? State.SightDelayRemaining : State.DeploymentTimerRemaining;
	PendingRoomState = MoveTemp(State); bHasPendingRoomState = true;
	return true;
}

bool UDeckEnemySpawnerComponent::FinalizeRoomState(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	AllocatedPlanSignature = GetPlanSignature();
	auto Find = [&RegisteredActors](const FGuid& Id) -> AActor*
	{
		AActor* const* Found = RegisteredActors.Find(Id); return Found ? *Found : nullptr;
	};
	for (const FGuid& Id : PendingRoomState.EnemyPoolIds)
	{
		ADeckEnemy* Enemy = Cast<ADeckEnemy>(Find(Id));
		if (!Enemy || EnemyPool.Contains(Enemy)) { OutError = FString::Printf(TEXT("Deck pool enemy missing/duplicate: %s"), *Id.ToString()); return false; }
		EnemyPool.Add(Enemy); Enemy->SetHostShip(GetHostShip());
	}
	for (const FGuid& Id : PendingRoomState.AliveDeployedEnemyIds)
	{
		ADeckEnemy* Enemy = Cast<ADeckEnemy>(Find(Id));
		if (!Enemy || !EnemyPool.Contains(Enemy) || Enemy->IsPoolDeathHandled()) { OutError = TEXT("Invalid live deck pool reference"); return false; }
		AliveDeployedEnemies.Add(Enemy);
	}
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		Enemy->RestoreLegacyPoolActivity(AliveDeployedEnemies.Contains(Enemy));
		if (!Enemy->IsPoolActive()) Enemy->ResetBalanceForReuse(); // Normalize inactive old pool flags without healing.
	}
	for (const FSWRoomDeckPointState& Saved : PendingRoomState.Points)
	{
		if (!GetWaypoint(Saved.PointId) || (Saved.OccupantId.IsValid() && !Find(Saved.OccupantId))
			|| (Saved.ReservedById.IsValid() && !Find(Saved.ReservedById))) { OutError = TEXT("Invalid restored deck point reference"); return false; }
		FDeckPointRuntimeState& Point = PointRuntimeStates.FindOrAdd(Saved.PointId);
		Point.Occupant = Find(Saved.OccupantId); Point.ReservedBy = Find(Saved.ReservedById); Point.ReservationSerial = Saved.ReservationSerial;
	}
	DeploymentTriggerShip = Cast<AShip>(Find(PendingRoomState.TriggerShipId));
	DeploymentInitialTarget = Find(PendingRoomState.InitialTargetId);
	if (DeploymentTicket.bValid && !EnemyPool.Contains(Cast<ADeckEnemy>(Find(DeploymentTicket.PoolActorId))))
	{ OutError = TEXT("Restored deployment ticket has no pool actor"); return false; }
	bHasPendingRoomState = false;
	// No timers or AI are resumed here: the subsystem broadcasts only after EVERY actor finalizes.
	const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	if (!Room || !Room->IsRestoringSnapshot()) RequestReadinessEvaluation();
	return true;
}

void UDeckEnemySpawnerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Shutdown();
	Super::EndPlay(EndPlayReason);
}

AEnemyShip* UDeckEnemySpawnerComponent::GetHostShip() const
{
	return Cast<AEnemyShip>(GetOwner());
}

bool UDeckEnemySpawnerComponent::IsEnabled() const
{
	return bEnableSpawning;
}

bool UDeckEnemySpawnerComponent::ValidateAuthoredSpawnSlot(
	const FDeckEnemySpawnSlot& AuthoredSlot,
	int32 SlotIndex,
	FDeckEnemyDeploymentSlot& OutSlot,
	bool bLogErrors) const
{
	OutSlot = FDeckEnemyDeploymentSlot();
	AEnemyShip* Host = GetHostShip();
	if (!Host || !AuthoredSlot.EnemyClass)
	{
		if (bLogErrors)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] Invalid explicit spawn slot. Ship=%s Slot=%d Reason=%s"),
				*GetNameSafe(Host), SlotIndex,
				AuthoredSlot.EnemyClass ? TEXT("MissingHost") : TEXT("MissingEnemyClass")); */ {}
		}
		return false;
	}

	const int32 PointId = AuthoredSlot.SpawnPointId;
	UDeckWaypointComponent* Waypoint = GetWaypoint(PointId);
	if (!IsValid(Waypoint))
	{
		if (bLogErrors)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] Invalid explicit spawn slot. Ship=%s Slot=%d Class=%s SpawnPointId=%d Reason=UnknownSpawnPointId"),
				*GetNameSafe(Host), SlotIndex, *GetNameSafe(AuthoredSlot.EnemyClass.Get()), PointId); */ {}
		}
		return false;
	}

	if (!Waypoint->CanSpawnEnemy())
	{
		if (bLogErrors) /* Diagnostics muted after pooling/crew verification.
UE_LOG(LogTemp, Error, TEXT("[DeckEnemySpawner] Ship=%s Slot=%d PointId=%d Reason=SpawnDisabled"), *GetNameSafe(Host), SlotIndex, PointId); */ {}
		return false;
	}
	const auto* CDO = AuthoredSlot.EnemyClass->GetDefaultObject<ADeckEnemy>();
	const auto& RowHandle = AuthoredSlot.StatsRow.IsNull() ? CDO->DefaultStatsRow : AuthoredSlot.StatsRow;
	if (!RowHandle.IsNull())
	{
		const auto* Row = RowHandle.GetRow<FEnemyBaseStatsRow>(TEXT("Deck spawn plan"));
		const auto* Combat = Row && !Row->CombatSettings.IsNull() ? Row->CombatSettings.GetRow<FEnemyCombatBalanceRow>(TEXT("Deck combat plan")) : nullptr;
		if (!Row || !Row->IsValid() || (!Row->CombatSettings.IsNull() && (!Combat || !Combat->IsValid())))
		{
			if (bLogErrors) /* Diagnostics muted after pooling/crew verification.
UE_LOG(LogTemp, Error, TEXT("[DeckEnemySpawner] Ship=%s Slot=%d Reason=InvalidStatsRow"), *GetNameSafe(Host), SlotIndex); */ {}
			return false;
		}
	}

	OutSlot.EnemyClass = AuthoredSlot.EnemyClass;
	OutSlot.StatsRow = AuthoredSlot.StatsRow;
	OutSlot.SpawnPointId = PointId;
	return true;
}

bool UDeckEnemySpawnerComponent::BuildDeploymentPlan(
	TArray<FDeckEnemyDeploymentSlot>& OutPlan,
	bool bLogErrors) const
{
	OutPlan.Reset();
	if (!bEnableSpawning || SpawnPlan.IsEmpty())
	{
		if (bLogErrors)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Warning,
				TEXT("[DeckEnemySpawner] SpawnPlan is disabled or empty. Ship=%s"), *GetNameSafe(GetHostShip())); */ {}
		}
		return false;
	}

	if (SpawnPlan.Num() > 32)
	{
		if (bLogErrors)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] SpawnPlan exceeds the 32-enemy limit. Ship=%s Count=%d"),
				*GetNameSafe(GetHostShip()), SpawnPlan.Num()); */ {}
		}
		return false;
	}

	bool bPlanIsValid = true;
	TSet<int32> AssignedPointIds;
	for (int32 SlotIndex = 0; SlotIndex < SpawnPlan.Num(); ++SlotIndex)
	{
		FDeckEnemyDeploymentSlot DeploymentSlot;
		if (!ValidateAuthoredSpawnSlot(
			SpawnPlan[SlotIndex], SlotIndex, DeploymentSlot, bLogErrors))
		{
			bPlanIsValid = false;
			continue;
		}
		if (AssignedPointIds.Contains(DeploymentSlot.SpawnPointId))
		{
			bPlanIsValid = false;
			if (bLogErrors)
			{
				/* Diagnostics muted after pooling/crew verification.
				UE_LOG(LogTemp, Error,
					TEXT("[DeckEnemySpawner] Duplicate SpawnPointId in SpawnPlan. Ship=%s Slot=%d PointId=%d"),
					*GetNameSafe(GetHostShip()), SlotIndex, DeploymentSlot.SpawnPointId); */ {}
			}
			continue;
		}
		AssignedPointIds.Add(DeploymentSlot.SpawnPointId);
		OutPlan.Add(MoveTemp(DeploymentSlot));
	}

	if (!bPlanIsValid || OutPlan.IsEmpty())
	{
		OutPlan.Reset();
		return false;
	}
	return true;
}

void UDeckEnemySpawnerComponent::InitializeWaypoints()
{
	WaypointsById.Reset();
	SpawnWaypoints.Reset();
	PointRuntimeStates.Reset();

	AEnemyShip* Host = GetHostShip();
	if (!Host)
	{
		return;
	}

	TArray<UDeckWaypointComponent*> Components;
	Host->GetComponents<UDeckWaypointComponent>(Components);
	for (UDeckWaypointComponent* Waypoint : Components)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}

		const int32 WaypointId = Waypoint->GetWaypointId();
		if (WaypointsById.Contains(WaypointId))
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] Duplicate WaypointId. Ship=%s WaypointId=%d Component=%s"),
				*GetNameSafe(Host), WaypointId, *GetNameSafe(Waypoint)); */ {}
			continue;
		}

		if (Host->GetShipDeckMesh() && !Waypoint->IsAttachedTo(Host->GetShipDeckMesh()))
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Warning,
				TEXT("[DeckEnemySpawner] Waypoint is not attached below ShipDeckMesh. Ship=%s Waypoint=%s"),
				*GetNameSafe(Host), *GetNameSafe(Waypoint)); */ {}
		}

		WaypointsById.Add(WaypointId, Waypoint);
		if (Waypoint->CanSpawnEnemy())
		{
			SpawnWaypoints.Add(Waypoint);
		}
	}

	SpawnWaypoints.Sort([](const UDeckWaypointComponent& Left, const UDeckWaypointComponent& Right)
	{
		return Left.GetWaypointId() < Right.GetWaypointId();
	});

}

void UDeckEnemySpawnerComponent::BeginQueuedPoolInitialization()
{
	const double InitStart = FPlatformTime::Seconds();
	const TCHAR* InitResult = TEXT("SkippedHostDisabledExistingPoolOrRestore");
	ON_SCOPE_EXIT
	{
		if (EnemyShipDiagnostics::CrewEnabled()) /* EmitCrewDiagnostic(TEXT("PoolInit"), FString::Printf(TEXT("Result=%s PlannedSlots=%d SpawnWaypoints=%d DurationMs=%.3f"),
			InitResult, QueuedPoolPlan.Num(), SpawnWaypoints.Num(), (FPlatformTime::Seconds() - InitStart) * 1000.)); */ {}
	};
	QueuedPoolPlan.Reset();
	QueuedPoolIndex = 0;
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsEnabled() || !EnemyPool.IsEmpty()
		|| GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
	{
		return;
	}
	if (!Host->GetDeckWalkAreaComponent() || !Host->GetDeckWalkAreaComponent()->IsReady()) { InitResult = TEXT("WalkNotReady"); return; }
	if (SpawnWaypoints.IsEmpty())
	{
		InitResult = TEXT("NoSpawnWaypoints");
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Warning,
			TEXT("[DeckEnemySpawner] No spawn waypoint exists. Ship=%s"), *GetNameSafe(Host)); */ {}
		return;
	}

	auto& ResolvedPlan = QueuedPoolPlan;
	if (!BuildDeploymentPlan(ResolvedPlan, true))
	{
		InitResult = TEXT("InvalidPlan");
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Warning,
			TEXT("[DeckEnemySpawner] Spawn plan could not be resolved. Ship=%s"), *GetNameSafe(Host)); */ {}
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	AllocatedPlanSignature = GetPlanSignature();
	InitResult = TEXT("PlanReady");

}

bool UDeckEnemySpawnerComponent::StepQueuedPoolInitialization()
{
	// Pooling fix: one crew allocation per queue step; this still includes an indivisible FinishSpawning.
	// Copy the slot before construction callbacks, which may reset/reallocate QueuedPoolPlan.
	AEnemyShip* Host = GetHostShip();
	UWorld* World = GetWorld();
	if (!Host || !World || QueuedPoolIndex >= QueuedPoolPlan.Num()) { QueuedPoolPlan.Reset(); return true; }
	const int32 SlotIndex = QueuedPoolIndex++;
	const FDeckEnemyDeploymentSlot Slot = QueuedPoolPlan[SlotIndex];
	const double SlotStart = FPlatformTime::Seconds();
	double ShellMs = 0.0, PrepareMs = 0.0, FinishMs = 0.0;
	ADeckEnemy* CreatedEnemy = nullptr;
	const TCHAR* SlotResult = TEXT("InvalidSpawnTransform");
	ON_SCOPE_EXIT
	{
		if (EnemyShipDiagnostics::CrewEnabled()) /* EmitCrewDiagnostic(TEXT("PoolSlot"), FString::Printf(
			TEXT("Result=%s SlotIndex=%d SpawnPoint=%d Class=%s Enemy=%s Active=%d Hidden=%d Collision=%d Tick=%d TotalMs=%.3f ShellMs=%.3f PrepareBalanceMs=%.3f FinishSpawnMs=%.3f"),
			SlotResult, SlotIndex, Slot.SpawnPointId, *GetNameSafe(Slot.EnemyClass.Get()), *GetNameSafe(CreatedEnemy),
			IsValid(CreatedEnemy) && CreatedEnemy->IsPoolActive(), IsValid(CreatedEnemy) && CreatedEnemy->IsHidden(), IsValid(CreatedEnemy) && CreatedEnemy->GetActorEnableCollision(),
			IsValid(CreatedEnemy) && CreatedEnemy->IsActorTickEnabled(), (FPlatformTime::Seconds() - SlotStart) * 1000., ShellMs, PrepareMs, FinishMs)); */ {}
	};
		UDeckWaypointComponent* InitialWaypoint = GetWaypoint(Slot.SpawnPointId);
		const ADeckEnemy* EnemyCDO = Slot.EnemyClass
			? Slot.EnemyClass->GetDefaultObject<ADeckEnemy>()
			: nullptr;
		FTransform InitialTransform;
		if (!InitialWaypoint || !EnemyCDO
			|| !ResolveEnemySpawnTransform(InitialWaypoint, *EnemyCDO, InitialTransform))
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Warning,
				TEXT("[DeckEnemySpawner] No valid initial transform. Ship=%s Class=%s Slot=%d PointId=%d"),
				*GetNameSafe(Host), *GetNameSafe(Slot.EnemyClass.Get()), SlotIndex, Slot.SpawnPointId); */ {}
			return QueuedPoolIndex >= QueuedPoolPlan.Num();
		}

		const double ShellStart = FPlatformTime::Seconds();
		ADeckEnemy* PooledEnemy = World->SpawnActorDeferred<ADeckEnemy>(
			Slot.EnemyClass,
			InitialTransform,
			Host,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		ShellMs = (FPlatformTime::Seconds() - ShellStart) * 1000.;
		CreatedEnemy = PooledEnemy;
		SlotResult = TEXT("AllocationFailed");
		if (!PooledEnemy)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] Failed to allocate pool actor. Ship=%s Class=%s Slot=%d"),
				*GetNameSafe(Host), *GetNameSafe(Slot.EnemyClass.Get()), SlotIndex); */ {}
			return QueuedPoolIndex >= QueuedPoolPlan.Num();
		}

		const double PrepareStart = FPlatformTime::Seconds();
		PooledEnemy->PrepareForPool();
		PooledEnemy->ConfigureSpawnBalance(Slot.StatsRow);
		PrepareMs = (FPlatformTime::Seconds() - PrepareStart) * 1000.;
		const double FinishStart = FPlatformTime::Seconds();
		PooledEnemy->FinishSpawning(InitialTransform);
		FinishMs = (FPlatformTime::Seconds() - FinishStart) * 1000.;
		SlotResult = TEXT("DestroyedDuringConstruction");
		if (!IsValid(PooledEnemy)) return QueuedPoolIndex >= QueuedPoolPlan.Num();
		PooledEnemy->SetHostShip(Host);
		PooledEnemy->DeactivateToPool();
		EnemyPool.Add(PooledEnemy);
		Host->RegisterCrewEnemy(PooledEnemy);
		SlotResult = TEXT("ReadyInactive");
	return QueuedPoolIndex >= QueuedPoolPlan.Num();
}

void UDeckEnemySpawnerComponent::InitializePool()
{
	// Remaining cost risk: this fallback drains all slots synchronously. Pool wake uses the step API;
	// callers outside that queue can still construct several crew Actors in one frame.
	BeginQueuedPoolInitialization();
	while (!StepQueuedPoolInitialization()) {}
}

void UDeckEnemySpawnerComponent::GetPooledEnemies(TArray<ADeckEnemy*>& OutEnemies) const
{
	OutEnemies.Reset(EnemyPool.Num());
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy)) OutEnemies.Add(Enemy);
	}
}

bool UDeckEnemySpawnerComponent::AuditBundleCrew(bool bRequireCompletePool, FString& Error) const
{
	TArray<FDeckEnemyDeploymentSlot> Plan;
	const bool bEnabled = IsEnabled();
	const bool bPlanValid = !bEnabled || BuildDeploymentPlan(Plan, false);
	int32 ClassMismatches = 0;
	int32 StatsMismatches = 0;
	for (int32 Index = 0; Index < EnemyPool.Num(); ++Index)
	{
		ADeckEnemy* Enemy = EnemyPool[Index];
		const bool bClassMatch = IsValid(Enemy) && Plan.IsValidIndex(Index) && Enemy->GetClass() == Plan[Index].EnemyClass;
		if (!bClassMatch) ++ClassMismatches;
		const FDataTableRowHandle ActualRow = IsValid(Enemy) ? Enemy->GetEffectiveSpawnStatsRow() : FDataTableRowHandle();
		const FDataTableRowHandle ExpectedRow = Plan.IsValidIndex(Index) && Plan[Index].EnemyClass
			? (Plan[Index].StatsRow.IsNull() ? Plan[Index].EnemyClass->GetDefaultObject<ADeckEnemy>()->DefaultStatsRow : Plan[Index].StatsRow)
			: FDataTableRowHandle();
		const bool bStatsMatch = IsValid(Enemy) && Plan.IsValidIndex(Index)
			&& ActualRow.DataTable == ExpectedRow.DataTable && ActualRow.RowName == ExpectedRow.RowName;
		if (!bStatsMatch) ++StatsMismatches;
		/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=CrewSlot Actor=%s Slot=%d ExpectedClass=%s ActualClass=%s StatsTable=%s StatsRow=%s ActualStatsTable=%s ActualStatsRow=%s Point=%d ClassMatch=%d StatsMatch=%d"),
			*GetNameSafe(GetHostShip()), Index, Plan.IsValidIndex(Index) ? *GetPathNameSafe(Plan[Index].EnemyClass.Get()) : TEXT("None"),
			IsValid(Enemy) ? *Enemy->GetClass()->GetPathName() : TEXT("None"),
			*GetPathNameSafe(ExpectedRow.DataTable), *ExpectedRow.RowName.ToString(),
			*GetPathNameSafe(ActualRow.DataTable), *ActualRow.RowName.ToString(),
			Plan.IsValidIndex(Index) ? Plan[Index].SpawnPointId : INDEX_NONE, bClassMatch, bStatsMatch); */
	}
	const bool bComplete = bPlanValid && EnemyPool.Num() == Plan.Num() && ClassMismatches == 0 && StatsMismatches == 0;
	/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=CrewReady Actor=%s Enabled=%d Planned=%d Actual=%d PlanValid=%d ClassMismatches=%d StatsMismatches=%d Complete=%d RequireComplete=%d"),
		*GetNameSafe(GetHostShip()), bEnabled, Plan.Num(), EnemyPool.Num(), bPlanValid, ClassMismatches, StatsMismatches, bComplete, bRequireCompletePool); */
	if (bRequireCompletePool && !bComplete) { Error = TEXT("Enemy ship crew pool does not match the assigned spawn plan"); return false; }
	return true;
}

void UDeckEnemySpawnerComponent::ReconcileBundleProfile()
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || EnemyPool.IsEmpty()) return;
	if (bEnableSpawning && AllocatedPlanSignature == GetPlanSignature()) return;
	ClearPoolForIdle();
	/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=CrewProfileChanged Actor=%s RemovedCrew=%d NewSlots=%d Enabled=%d"),
		*Host->GetName(), PreviousCount, SpawnPlan.Num(), bEnableSpawning); */
}

bool UDeckEnemySpawnerComponent::StepClearPoolForIdle()
{
	// Save/restore fix: retire crew AND controllers from snapshot tracking before destruction.
	// These belong to the old placement; retaining their stable IDs can restore obsolete crew later.
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority()) return true;
	CancelDeployment();
	// const int32 PreviousCount = EnemyPool.Num();
	QueuedPoolPlan.Reset();
	if (!EnemyPool.IsEmpty())
	{
		ADeckEnemy* Enemy = EnemyPool.Pop(EAllowShrinking::No);
		if (IsValid(Enemy))
		{
			Host->UnregisterCrewEnemy(Enemy);
			ReleaseAllPointsFor(Enemy);
			Enemy->Tags.AddUnique(TEXT("SWEnemyShipPoolReleased"));
			if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->ReleaseActorForPooling(Enemy);
			if (AController* Controller = Enemy->GetController())
			{
				Controller->Tags.AddUnique(TEXT("SWEnemyShipPoolReleased"));
				if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->ReleaseActorForPooling(Controller);
				Controller->Destroy();
			}
			Enemy->Destroy();
		}
		return false;
	}
	EnemyPool.Reset();
	PointRuntimeStates.Reset();
	AllocatedPlanSignature.Reset();
	ResetForNewEncounter();
	return true;
}

void UDeckEnemySpawnerComponent::ClearPoolForIdle()
{
	while (!StepClearPoolForIdle()) {}
}

void UDeckEnemySpawnerComponent::Shutdown()
{
	bShuttingDown = true;
	UnbindLifecycleDelegates();
	CancelDeployment();
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy))
		{
			if (AEnemyShip* Host = GetHostShip()) Host->UnregisterCrewEnemy(Enemy);
			ReleaseAllPointsFor(Enemy);
			Enemy->Destroy();
		}
	}
	EnemyPool.Reset();
	AliveDeployedEnemies.Reset();
	PointRuntimeStates.Reset();
	DeploymentState = EDeckEnemyDeploymentState::Idle;
	bHasDeployedEnemy = false;
	bAllDeployedEnemiesDefeated = false;
}

void UDeckEnemySpawnerComponent::CancelDeployment()
{
	if (SpawnRequestState == EDeckEnemySpawnRequestState::PendingReadiness || SpawnRequestState == EDeckEnemySpawnRequestState::Running)
		/* EmitCrewDiagnostic(TEXT("RequestCancelled"), TEXT("LastReason=") + LastSpawnReason.ToString()); */ {}
	++ExecutionEpoch;
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SightDelayTimerHandle);
		World->GetTimerManager().ClearTimer(DeploymentTimerHandle);
		World->GetTimerManager().ClearTimer(ReadinessTimerHandle);
	}
	bReadinessEvaluationQueued = false;
	bDeploymentPaused = false;
	PausedDeploymentDelay = 0.f;
	DeploymentQueue.Reset();
	DeploymentTriggerShip.Reset();
	DeploymentInitialTarget.Reset();
	ReactionReadyTime = PendingSightExpiry = ReadinessExpiry = TargetWaitExpiry = 0.;
	if (SpawnRequestState == EDeckEnemySpawnRequestState::PendingReadiness || SpawnRequestState == EDeckEnemySpawnRequestState::Running)
	{
		SpawnRequestState = EDeckEnemySpawnRequestState::Cancelled;
		DeploymentState = EDeckEnemyDeploymentState::Failed;
	}
}

bool UDeckEnemySpawnerComponent::RequestDeployment(AShip* TriggeringPlayerShip, AActor* InitialCombatTarget)
{
	AEnemyShip* Host = GetHostShip();
	const auto* Room = GetWorld() ? GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>() : nullptr;
	if (!Host || !Host->HasAuthority() || bShuttingDown || Host->IsDeathHandled() || Host->IsSinking()
		|| Host->IsCrewDefeated() || Host->IsStoryGateDormant() || !IsEnabled() || (Room && Room->IsRestoringSnapshot())
		|| !IsValid(TriggeringPlayerShip) || TriggeringPlayerShip == Host || TriggeringPlayerShip->IsSinking()
		|| TriggeringPlayerShip->IsEnemyShipForEffects() || !TriggeringPlayerShip->ActorHasTag(TEXT("Player"))
		|| TriggeringPlayerShip->ActorHasTag(TEXT("Enemy")) || (InitialCombatTarget && !IsValid(InitialCombatTarget)))
	{
		/* EmitCrewDiagnostic(TEXT("RequestRejected"), TEXT("Reason=HostTargetOrRestoreGate"), TriggeringPlayerShip); */ {} return false;
	}
	if (SpawnRequestState == EDeckEnemySpawnRequestState::Finished) { /* EmitCrewDiagnostic(TEXT("RequestRejected"), TEXT("Reason=AlreadyFinished"), TriggeringPlayerShip); */ {} return false; }
	if (SpawnRequestState == EDeckEnemySpawnRequestState::PendingReadiness || SpawnRequestState == EDeckEnemySpawnRequestState::Running)
	{
		RequestReadinessEvaluation(); return false; // Merge without restarting deadlines or clearing deployed crew.
	}
	if (!BuildDeploymentPlan(DeploymentQueue, true))
	{
		SetWaitReason(TEXT("InvalidSpawnPlan")); DeploymentState = EDeckEnemyDeploymentState::Failed;
		SpawnRequestState = EDeckEnemySpawnRequestState::Finished; return false;
	}
	if (SlotResults.IsEmpty()) { SlotResults.Init(0, DeploymentQueue.Num()); SlotEnemyIds.SetNum(DeploymentQueue.Num()); }
	if (SlotResults.Num() != DeploymentQueue.Num()) { SetWaitReason(TEXT("PlanMismatch")); SpawnRequestState = EDeckEnemySpawnRequestState::Finished; return false; }
	DeploymentQueueIndex = 0;
	DeploymentFailureCount = 0;
	for (uint8 Result : SlotResults) if (Result == 2) ++DeploymentFailureCount;
	while (SlotResults.IsValidIndex(DeploymentQueueIndex) && SlotResults[DeploymentQueueIndex] != 0) ++DeploymentQueueIndex;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex)) { FinishDeployment(); return false; }
	++RequestId;
	++ExecutionEpoch;
	DeploymentTriggerShip = TriggeringPlayerShip;
	DeploymentInitialTarget = InitialCombatTarget;
	CurrentRetryCount = 0;
	DeploymentState = EDeckEnemyDeploymentState::Preparing;
	SpawnRequestState = EDeckEnemySpawnRequestState::PendingReadiness;
	const double Now = GetWorld()->GetTimeSeconds();
	ReactionReadyTime = Now + FMath::Max(0.f, SightActivationDelay);
	PendingSightExpiry = Now + FMath::Max(FMath::Max(0.1f, PendingSightLifetime), GetRemainingSpawnStartDelay() + SightActivationDelay);
	ReadinessExpiry = Now + FMath::Max(FMath::Max(1.f, ReadinessTimeout), GetRemainingSpawnStartDelay() + SightActivationDelay + 1.f);
	TargetWaitExpiry = 0.;
	bDeploymentPaused = false;
	/* EmitCrewDiagnostic(TEXT("RequestAccepted"), FString::Printf(TEXT("SpawnStartDelay=%.3f SightActivationDelay=%.3f PendingSightLifetime=%.3f"), SpawnStartDelay, SightActivationDelay, PendingSightLifetime), TriggeringPlayerShip); */ {}
	RequestReadinessEvaluation();
	return true;
}

void UDeckEnemySpawnerComponent::BeginDeployment()
{
	if (!GetHostShip() || !GetHostShip()->CanDeployDeckEnemies()) { RequestReadinessEvaluation(); return; }
	/* EmitCrewDiagnostic(TEXT("BeginDeployment")); */ {}
	DeploymentState = EDeckEnemyDeploymentState::Deploying;
	SpawnRequestState = EDeckEnemySpawnRequestState::Running;
	ReadinessExpiry = 0.;
	DeployNextEnemy();
}

bool UDeckEnemySpawnerComponent::CreateDeploymentTicket(float DelaySeconds)
{
	if (DeploymentTicket.bValid && DeploymentTicket.QueueIndex == DeploymentQueueIndex) return true;
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex)) return false;
	const FDeckEnemyDeploymentSlot& Slot = DeploymentQueue[DeploymentQueueIndex];
	ADeckEnemy* Enemy = FindInactiveEnemy(Slot.EnemyClass);
	UDeckWaypointComponent* Waypoint = GetWaypoint(Slot.SpawnPointId);
	FTransform Transform;
	if (!Enemy || !Waypoint || !ResolveEnemySpawnTransform(Waypoint, *Enemy, Transform)) return false;
	const USWRoomSnapshotComponent* Snapshot = Enemy->FindComponentByClass<USWRoomSnapshotComponent>();
	if (!Snapshot || !Snapshot->StableId.IsValid()) return false;
	DeploymentTicket.bValid = true;
	DeploymentTicket.PoolActorId = Snapshot->StableId;
	DeploymentTicket.QueueIndex = DeploymentQueueIndex;
	DeploymentTicket.WorldTransform = Transform;
	DeploymentTicket.RemainingSeconds = DelaySeconds;
	return true;
}

ADeckEnemy* UDeckEnemySpawnerComponent::FindInactiveEnemy(
	TSubclassOf<ADeckEnemy> RequiredClass) const
{
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy) && !Enemy->IsPoolActive()
			&& (!RequiredClass || Enemy->GetClass() == RequiredClass.Get()))
		{
			return Enemy;
		}
	}
	return nullptr;
}

void UDeckEnemySpawnerComponent::DeployNextEnemy()
{
	GetWorld()->GetTimerManager().ClearTimer(DeploymentTimerHandle);
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->CanDeployDeckEnemies()) { RequestReadinessEvaluation(); return; }
	while (SlotResults.IsValidIndex(DeploymentQueueIndex) && SlotResults[DeploymentQueueIndex] != 0) ++DeploymentQueueIndex;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex)) { FinishDeployment(); return; }
	const FDeckEnemyDeploymentSlot Slot = DeploymentQueue[DeploymentQueueIndex]; // Callbacks may replace the queue.
	const uint32 Epoch = ExecutionEpoch, Generation = EncounterGeneration;
	ADeckEnemy* Enemy = FindInactiveEnemy(Slot.EnemyClass);
	if (!Enemy) { HandleDeploymentFailure(TEXT("PoolActorBusy"), true); return; }
	FTransform Transform;
	if (!ResolveEnemySpawnTransform(GetWaypoint(Slot.SpawnPointId), *Enemy, Transform))
	{
		HandleDeploymentFailure(TEXT("NoWalkableSpawnFloor"), false); return;
	}
	FDeckPointReservation Reservation;
	if (!TryReservePoint(Slot.SpawnPointId, Enemy, Reservation)) { HandleDeploymentFailure(TEXT("PointUnavailable"), true); return; }
	ADeckEnemy* ActivatedEnemy = nullptr;
	if (!ActivateSpecificEnemyAtReservation(*Enemy, Reservation, DeploymentInitialTarget.Get(), ActivatedEnemy,
		&Transform, Slot.StatsRow, DeploymentQueueIndex))
	{
		ReleasePointReservation(Reservation);
		if (Epoch == ExecutionEpoch && Generation == EncounterGeneration) HandleDeploymentFailure(ActivationFailureReason, bActivationFailureRetryable);
		return;
	}
	if (Epoch != ExecutionEpoch || Generation != EncounterGeneration) return;
	++DeploymentQueueIndex;
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	CurrentRetryCount = 0;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex)) { FinishDeployment(); return; }
	ScheduleDeployment(FMath::Max(0.05f, ActivationInterval), false);
}

void UDeckEnemySpawnerComponent::HandleDeploymentFailure(FName Reason, bool bRetryable)
{
	SetWaitReason(Reason);
	++CurrentRetryCount;
	if (bRetryable && CurrentRetryCount <= FMath::Max(0, MaxSpawnRetries))
	{
		ScheduleDeployment(FMath::Max(0.05f, SpawnRetryInterval), false); return;
	}
	/* Diagnostics muted after pooling/crew verification.
	UE_LOG(LogTemp, Warning, TEXT("[DeckEnemySpawner] Deployment entry abandoned. Ship=%s Generation=%u Request=%u Slot=%d PointId=%d Reason=%s Retryable=%d"),
		*GetNameSafe(GetHostShip()), EncounterGeneration, RequestId, DeploymentQueueIndex,
		DeploymentQueue.IsValidIndex(DeploymentQueueIndex) ? DeploymentQueue[DeploymentQueueIndex].SpawnPointId : INDEX_NONE,
		*Reason.ToString(), bRetryable); */ {}
	if (SlotResults.IsValidIndex(DeploymentQueueIndex)) SlotResults[DeploymentQueueIndex] = 2;
	++DeploymentFailureCount;
	++DeploymentQueueIndex;
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	CurrentRetryCount = 0;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex)) { FinishDeployment(); return; }
	ScheduleDeployment(FMath::Max(0.05f, ActivationInterval), false);
}

void UDeckEnemySpawnerComponent::FinishDeployment()
{
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	GetWorld()->GetTimerManager().ClearTimer(ReadinessTimerHandle);
	bReadinessEvaluationQueued = false;
	bDeploymentPaused = false;
	ReactionReadyTime = PendingSightExpiry = ReadinessExpiry = TargetWaitExpiry = 0.;
	SpawnRequestState = EDeckEnemySpawnRequestState::Finished;
	const int32 SuccessfulCount = SlotResults.FilterByPredicate([](uint8 Result) { return Result == 1; }).Num();
	DeploymentState = SuccessfulCount <= 0 ? EDeckEnemyDeploymentState::Failed
		: DeploymentFailureCount > 0 ? EDeckEnemyDeploymentState::CompletedWithFailures : EDeckEnemyDeploymentState::Completed;
	/* Diagnostics muted after pooling/crew verification.
	UE_LOG(LogTemp, Log, TEXT("[DeckEnemySpawner] Ship=%s Generation=%u Request=%u Result=%d Activated=%d Failed=%d"),
		*GetNameSafe(GetHostShip()), EncounterGeneration, RequestId, static_cast<int32>(DeploymentState), SuccessfulCount, DeploymentFailureCount); */ {}
	EvaluateAllEnemiesDefeated();
}

int32 UDeckEnemySpawnerComponent::GetAliveDeployedEnemyCount() const
{
	int32 AliveCount = 0;
	for (const TWeakObjectPtr<ADeckEnemy>& Enemy : AliveDeployedEnemies)
	{
		AliveCount += Enemy.IsValid() ? 1 : 0;
	}
	return AliveCount;
}

void UDeckEnemySpawnerComponent::ResetForNewEncounter()
{
	if (!GetHostShip() || !GetHostShip()->HasAuthority()) return;
	CancelDeployment();
	++EncounterGeneration;
	SlotResults.Reset();
	SlotEnemyIds.Reset();
	SpawnRequestState = EDeckEnemySpawnRequestState::None;
	LastSpawnReason = NAME_None;
	DeploymentTicket = FSWRoomDeckDeploymentTicket();
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy))
		{
			Enemy->ResetToFreshPoolState();
		}
	}
	AliveDeployedEnemies.Reset();
	DeploymentQueue.Reset();
	DeploymentState = EDeckEnemyDeploymentState::Idle;
	bHasDeployedEnemy = false;
	bAllDeployedEnemiesDefeated = false;
	DeploymentQueueIndex = 0;
	CurrentRetryCount = 0;
	DeploymentFailureCount = 0;
}

void UDeckEnemySpawnerComponent::RestartSpawnStartDelay()
{
	if (!GetHostShip() || !GetHostShip()->HasAuthority()) return;
	SpawnStartReadyTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, SpawnStartDelay);
	RequestReadinessEvaluation();
}

int32 UDeckEnemySpawnerComponent::GetLivingPooledEnemyCount() const
{
	int32 LivingCount = 0;
	for (const TObjectPtr<ADeckEnemy>& Enemy : EnemyPool)
	{
		if (IsValid(Enemy))
		{
			const UBaseHealthComponent* Health = Enemy->GetHealthComponent();
			LivingCount += (!Health || !Health->IsDead()) ? 1 : 0;
		}
	}
	return LivingCount;
}

void UDeckEnemySpawnerComponent::NotifyEnemyDefeated(ADeckEnemy* Enemy)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Enemy) || !EnemyPool.Contains(Enemy))
	{
		return;
	}

	AliveDeployedEnemies.Remove(Enemy);
	EvaluateAllEnemiesDefeated();
}

void UDeckEnemySpawnerComponent::EvaluateAllEnemiesDefeated()
{
	if (bAllDeployedEnemiesDefeated || !bHasDeployedEnemy
		|| (DeploymentState != EDeckEnemyDeploymentState::Completed
			&& DeploymentState != EDeckEnemyDeploymentState::CompletedWithFailures)
		|| GetAliveDeployedEnemyCount() > 0)
	{
		return;
	}

	bAllDeployedEnemiesDefeated = true;
	if (AEnemyShip* Host = GetHostShip())
	{
		Host->NotifyAllOwnedDeckEnemiesDefeated();
	}
}

UDeckWaypointComponent* UDeckEnemySpawnerComponent::GetWaypoint(int32 WaypointId) const
{
	const TObjectPtr<UDeckWaypointComponent>* Found = WaypointsById.Find(WaypointId);
	return Found ? Found->Get() : nullptr;
}

FVector UDeckEnemySpawnerComponent::GetWaypointWorldLocation(int32 WaypointId) const
{
	const UDeckWaypointComponent* Waypoint = GetWaypoint(WaypointId);
	const AEnemyShip* Host = GetHostShip();
	const UDeckWalkAreaComponent* Area = Host ? Host->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Floor;
	if (Waypoint && Area && Area->IsReady() && Area->ResolveWaypoint(*Waypoint, Floor))
	{
		return Area->ToWorld(Floor.LocalFloor);
	}
	return Waypoint ? Waypoint->GetComponentLocation()
		: (GetHostShip() ? GetHostShip()->GetActorLocation() : FVector::ZeroVector);
}

bool UDeckEnemySpawnerComponent::ResolveDeckCharacterTransform(int32 PointId, float CapsuleHalfHeight, FTransform& OutTransform) const
{
	const AEnemyShip* Host = GetHostShip();
	const UDeckWaypointComponent* Point = GetWaypoint(PointId);
	const UDeckWalkAreaComponent* Area = Host ? Host->GetDeckWalkAreaComponent() : nullptr;
	return Point && Area && Area->IsReady() && Area->ResolveSpawnTransform(*Point, CapsuleHalfHeight, OutTransform);
}

bool UDeckEnemySpawnerComponent::ResolveEnemySpawnTransform(
	const UDeckWaypointComponent* SpawnWaypoint,
	const ADeckEnemy& Enemy,
	FTransform& OutTransform) const
{
	if (!SpawnWaypoint)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	return ResolveDeckCharacterTransform(SpawnWaypoint->GetWaypointId(), HalfHeight, OutTransform);
}

bool UDeckEnemySpawnerComponent::IsPointAvailable(int32 WaypointId, const AActor* Requester) const
{
	(void)Requester;
	const AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !GetWaypoint(WaypointId))
	{
		return false;
	}
	const FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
	return !State || (!State->Occupant.IsValid() && !State->ReservedBy.IsValid());
}

void UDeckEnemySpawnerComponent::PrunePointRuntimeState()
{
	for (auto It = PointRuntimeStates.CreateIterator(); It; ++It)
	{
		FDeckPointRuntimeState& State = It.Value();
		if (!State.Occupant.IsValid())
		{
			State.Occupant.Reset();
		}
		if (!State.ReservedBy.IsValid())
		{
			State.ReservedBy.Reset();
			State.ReservationSerial = 0;
		}
		if (!State.Occupant.IsValid() && !State.ReservedBy.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UDeckEnemySpawnerComponent::TryReservePoint(
	int32 WaypointId,
	AActor* Requester,
	FDeckPointReservation& OutReservation)
{
	OutReservation.Reset();
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Requester) || !GetWaypoint(WaypointId))
	{
		return false;
	}
	PrunePointRuntimeState();
	FDeckPointRuntimeState& State = PointRuntimeStates.FindOrAdd(WaypointId);
	if ((State.Occupant.IsValid() && State.Occupant.Get() != Requester)
		|| (State.ReservedBy.IsValid() && State.ReservedBy.Get() != Requester))
	{
		return false;
	}
	if (State.ReservedBy.Get() == Requester && State.ReservationSerial != 0)
	{
		OutReservation.PointId = WaypointId;
		OutReservation.Serial = State.ReservationSerial;
		OutReservation.Requester = Requester;
		return true;
	}

	uint32 Serial = NextReservationSerial++;
	if (Serial == 0)
	{
		Serial = NextReservationSerial++;
	}
	State.ReservedBy = Requester;
	State.ReservationSerial = Serial;
	OutReservation.PointId = WaypointId;
	OutReservation.Serial = Serial;
	OutReservation.Requester = Requester;
	return true;
}

bool UDeckEnemySpawnerComponent::TryReserveEnemySpawnPoint(
	const FDeckEnemySpawnRequest& Request,
	FDeckPointReservation& OutReservation)
{
	OutReservation.Reset();
	AEnemyShip* Host = GetHostShip();
	AActor* Requester = Request.Requester.Get();
	UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	if (!Host || !Host->HasAuthority() || !IsValid(Requester) || !DeckMesh)
	{
		return false;
	}

	PrunePointRuntimeState();
	const UDeckWalkAreaComponent* Area = Host->GetDeckWalkAreaComponent();
	const bool bUseWalkArea = Area && Area->IsReady();
	if (!bUseWalkArea)
	{
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Warning, TEXT("[DeckEnemySpawner] Ship=%s Requester=%s Reason=WalkAreaNotReady"),
			*GetNameSafe(Host), *GetNameSafe(Requester)); */ {}
		return false;
	}
	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector RequesterLocal = DeckTransform.InverseTransformPosition(Requester->GetActorLocation());
	const AActor* Target = Request.Target.Get();
	const FVector TargetLocal = Target
		? DeckTransform.InverseTransformPosition(Target->GetActorLocation())
		: FVector::ZeroVector;
	TArray<int32> Candidates;
	for (const UDeckWaypointComponent* Waypoint : SpawnWaypoints)
	{
		if (!Waypoint)
		{
			continue;
		}
		const int32 PointId = Waypoint->GetWaypointId();
		FDeckWalkLocation Floor;
		if (!Area->ResolveWaypoint(*Waypoint, Floor)
			|| PointId == Request.ExcludedPointId
			|| !IsPointAvailable(PointId, Requester))
		{
			continue;
		}
		FDeckWalkLocation RequesterFloor;
		TArray<FDeckWalkLocation> Path;
		if (!Area->ResolveActorOnDeck(*Requester, RequesterFloor) || !Area->FindPath(RequesterFloor, Floor, Path)) continue;
		const FVector PointLocal = Floor.LocalFloor;
		const float RequesterDistance = FVector::Dist2D(PointLocal, RequesterLocal);
		const float TargetDistance = Target
			? FVector::Dist2D(PointLocal, TargetLocal)
			: TNumericLimits<float>::Max();
		if (RequesterDistance < FMath::Max(0.0f, Request.MinimumDistanceFromRequester)
			|| TargetDistance < FMath::Max(0.0f, Request.MinimumDistanceFromTarget))
		{
			continue;
		}
		Candidates.Add(PointId);
	}

	Candidates.Sort([this, &Request, Target, DeckTransform](int32 LeftId, int32 RightId)
	{
		const bool bLeftPreferred = Request.PreferredPointIds.Contains(LeftId);
		const bool bRightPreferred = Request.PreferredPointIds.Contains(RightId);
		if (bLeftPreferred != bRightPreferred)
		{
			return bLeftPreferred;
		}
		if (Target)
		{
			const FVector TargetLocal = DeckTransform.InverseTransformPosition(Target->GetActorLocation());
			const FVector LeftLocal = DeckTransform.InverseTransformPosition(GetWaypointWorldLocation(LeftId));
			const FVector RightLocal = DeckTransform.InverseTransformPosition(GetWaypointWorldLocation(RightId));
			const float LeftDistance = FVector::DistSquared2D(LeftLocal, TargetLocal);
			const float RightDistance = FVector::DistSquared2D(RightLocal, TargetLocal);
			if (!FMath::IsNearlyEqual(LeftDistance, RightDistance))
			{
				return LeftDistance > RightDistance;
			}
		}
		return LeftId < RightId;
	});

	for (const int32 PointId : Candidates)
	{
		if (TryReservePoint(PointId, Requester, OutReservation))
		{
			return true;
		}
	}
	return false;
}

bool UDeckEnemySpawnerComponent::CommitPointReservation(
	const FDeckPointReservation& Reservation,
	AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !Reservation.IsValid() || !IsValid(Occupant))
	{
		return false;
	}
	FDeckPointRuntimeState* State = PointRuntimeStates.Find(Reservation.PointId);
	if (!State || State->ReservedBy != Reservation.Requester
		|| State->ReservationSerial != Reservation.Serial
		|| (State->Occupant.IsValid() && State->Occupant.Get() != Occupant))
	{
		return false;
	}
	State->Occupant = Occupant;
	State->ReservedBy.Reset();
	State->ReservationSerial = 0;
	return true;
}

void UDeckEnemySpawnerComponent::ReleasePointReservation(FDeckPointReservation& Reservation)
{
	AEnemyShip* Host = GetHostShip();
	if (Host && Host->HasAuthority() && Reservation.PointId != INDEX_NONE)
	{
		if (FDeckPointRuntimeState* State = PointRuntimeStates.Find(Reservation.PointId);
			State && State->ReservedBy == Reservation.Requester
			&& State->ReservationSerial == Reservation.Serial)
		{
			State->ReservedBy.Reset();
			State->ReservationSerial = 0;
			if (!State->Occupant.IsValid())
			{
				PointRuntimeStates.Remove(Reservation.PointId);
			}
		}
	}
	Reservation.Reset();
}

bool UDeckEnemySpawnerComponent::TryOccupyPoint(int32 WaypointId, AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Occupant) || !GetWaypoint(WaypointId))
	{
		return false;
	}
	PrunePointRuntimeState();
	FDeckPointRuntimeState& State = PointRuntimeStates.FindOrAdd(WaypointId);
	if ((State.Occupant.IsValid() && State.Occupant.Get() != Occupant)
		|| (State.ReservedBy.IsValid() && State.ReservedBy.Get() != Occupant))
	{
		return false;
	}
	State.Occupant = Occupant;
	if (State.ReservedBy.Get() == Occupant)
	{
		State.ReservedBy.Reset();
		State.ReservationSerial = 0;
	}
	return true;
}

void UDeckEnemySpawnerComponent::ReleasePointOccupancy(int32 WaypointId, AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Occupant))
	{
		return;
	}
	if (FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
		State && State->Occupant.Get() == Occupant)
	{
		State->Occupant.Reset();
		if (!State->ReservedBy.IsValid())
		{
			PointRuntimeStates.Remove(WaypointId);
		}
	}
}

void UDeckEnemySpawnerComponent::ReleaseAllPointsFor(AActor* Actor)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !Actor)
	{
		return;
	}
	for (auto It = PointRuntimeStates.CreateIterator(); It; ++It)
	{
		FDeckPointRuntimeState& State = It.Value();
		if (State.Occupant.Get() == Actor)
		{
			State.Occupant.Reset();
		}
		if (State.ReservedBy.Get() == Actor)
		{
			State.ReservedBy.Reset();
			State.ReservationSerial = 0;
		}
		if (!State.Occupant.IsValid() && !State.ReservedBy.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UDeckEnemySpawnerComponent::ActivateEnemyAtPoint(
	int32 SpawnPointId,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (const AEnemyShip* Host = GetHostShip(); !Host || Host->IsCrewDefeated() || Host->IsStoryGateDormant()) return false;
	if (EnemyPool.IsEmpty())
	{
		InitializePool();
	}
	ADeckEnemy* Enemy = FindInactiveEnemy(nullptr);
	if (!Enemy)
	{
		return false;
	}
	FDeckPointReservation Reservation;
	if (!TryReservePoint(SpawnPointId, Enemy, Reservation))
	{
		return false;
	}
	return ActivateSpecificEnemyAtReservation(*Enemy, Reservation, InitialTarget, OutEnemy);
}

bool UDeckEnemySpawnerComponent::ActivateEnemyAtReservation(
	FDeckPointReservation& Reservation,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy,
	TSubclassOf<ADeckEnemy> RequiredClass,
	const FDataTableRowHandle& StatsRow)
{
	OutEnemy = nullptr;
	if (const AEnemyShip* Host = GetHostShip(); !Host || Host->IsCrewDefeated() || Host->IsStoryGateDormant()) return false;
	if (EnemyPool.IsEmpty())
	{
		InitializePool();
	}
	ADeckEnemy* Enemy = FindInactiveEnemy(RequiredClass);
	if (!Enemy)
	{
		ReleasePointReservation(Reservation);
		return false;
	}
	return ActivateSpecificEnemyAtReservation(*Enemy, Reservation, InitialTarget, OutEnemy, nullptr, StatsRow);
}


bool UDeckEnemySpawnerComponent::ActivateSpecificEnemyAtReservation(
	ADeckEnemy& Enemy, FDeckPointReservation& Reservation, AActor* InitialTarget, ADeckEnemy*& OutEnemy,
	const FTransform* ReservedTransform, const FDataTableRowHandle& StatsRow, int32 AutomaticSlotIndex)
{
	// Pooling fix: prepare hidden/inactive, validate the encounter and deck revision, commit the
	// point/alive/slot ledger, then publish collision and AI. Construction/activation callbacks may
	// change the host encounter, so the post-prepare epoch/generation checks must remain.
	OutEnemy = nullptr;
	ActivationFailureReason = NAME_None;
	bActivationFailureRetryable = false;
	auto Reject = [this, &Reservation, &Enemy](FName Reason, bool bRetryable = false)
	{
		ActivationFailureReason = Reason;
		bActivationFailureRetryable = bRetryable;
		SetWaitReason(Reason);
		if (EnemyShipDiagnostics::CrewEnabled()) /* EmitCrewDiagnostic(TEXT("ActivationRejected"), FString::Printf(TEXT("Enemy=%s Reason=%s Point=%d Retryable=%d"),
			*Enemy.GetName(), *Reason.ToString(), Reservation.PointId, bRetryable)); */ {}
		ReleasePointReservation(Reservation);
		return false;
	};
	AEnemyShip* Host = GetHostShip();
	if (bActivationInProgress) return Reject(TEXT("ActivationBusy"), true);
	if (!Host || !Host->CanDeployDeckEnemies() || !Reservation.IsValid() || GetRemainingSpawnStartDelay() > 0.f)
		return Reject(TEXT("HostNotReady"));
	if (Enemy.IsPoolActive()) return Reject(TEXT("PoolActorBusy"), true);
	if (InitialTarget && !Enemy.IsValidCombatTarget(InitialTarget)) return Reject(TEXT("InvalidCombatTarget"));
	TGuardValue<bool> Activating(bActivationInProgress, true);
	const uint32 Epoch = ExecutionEpoch, Generation = EncounterGeneration;
	const auto* Area = Host->GetDeckWalkAreaComponent();
	const int32 WalkRevision = Area ? Area->GetRevision() : INDEX_NONE;
	UDeckWaypointComponent* Point = GetWaypoint(Reservation.PointId);
	if (!Point || !Point->CanSpawnEnemy()) return Reject(TEXT("MissingSpawnPoint"));
	FTransform Transform;
	if (ReservedTransform) Transform = *ReservedTransform;
	if (!Area || !Area->IsReady() || (!ReservedTransform && !ResolveEnemySpawnTransform(Point, Enemy, Transform)) || Transform.ContainsNaN())
		return Reject(TEXT("NoWalkableSpawnFloor"));
	const auto* Capsule = Enemy.GetCapsuleComponent();
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 42.f;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DeckEnemySpawnClearance), false, &Enemy);
	Query.AddIgnoredActor(Host);
	if (GetWorld()->OverlapBlockingTestByChannel(Transform.GetLocation(), Transform.GetRotation(), ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Query)) return Reject(TEXT("CapsuleBlocked"), true);
	if (!Enemy.ConfigureSpawnBalance(StatsRow)) return Reject(TEXT("InactiveBalanceAlreadyApplied"));
	const int32 Seed = static_cast<int32>(HashCombine(static_cast<uint32>(RandomSeed),
		HashCombine(GetTypeHash(Host->GetFName()), static_cast<uint32>(ActivationSerial++))));
	if (!Enemy.PreparePoolActivation(Host, Reservation.PointId, Seed, &Transform))
	{
		Enemy.DeactivateToPool(); return Reject(TEXT("ActivationPreparationFailed"));
	}
	if (Epoch != ExecutionEpoch || Generation != EncounterGeneration || !Host->CanDeployDeckEnemies()
		|| !Area->IsReady() || Area->GetRevision() != WalkRevision)
	{
		Enemy.DeactivateToPool(); return Reject(TEXT("ObsoleteActivation"));
	}
	if (!CommitPointReservation(Reservation, &Enemy))
	{
		Enemy.DeactivateToPool(); return Reject(TEXT("InvalidPointCommit"));
	}
	const int32 PointId = Reservation.PointId;
	Reservation.Reset();
	// Record every authoritative claim before collision, AI, or reactivation delegates become visible.
	AliveDeployedEnemies.Add(&Enemy);
	bHasDeployedEnemy = true;
	bAllDeployedEnemiesDefeated = false;
	if (SlotResults.IsValidIndex(AutomaticSlotIndex))
	{
		SlotResults[AutomaticSlotIndex] = 1;
		if (const auto* Id = Enemy.FindComponentByClass<USWRoomSnapshotComponent>()) SlotEnemyIds[AutomaticSlotIndex] = Id->StableId;
	}
	if (InitialTarget) Enemy.SetCombatTarget(InitialTarget);
	if (!Enemy.CommitPoolActivation())
	{
		AliveDeployedEnemies.Remove(&Enemy);
		if (Generation == EncounterGeneration && SlotResults.IsValidIndex(AutomaticSlotIndex))
		{
			SlotResults[AutomaticSlotIndex] = 0; SlotEnemyIds[AutomaticSlotIndex].Invalidate();
		}
		ReleasePointOccupancy(PointId, &Enemy);
		Enemy.DeactivateToPool(); return Reject(TEXT("ActivationCommitFailed"));
	}
	OutEnemy = &Enemy;
	if (EnemyShipDiagnostics::CrewEnabled()) /* EmitCrewDiagnostic(TEXT("Activated"), FString::Printf(TEXT("Enemy=%s EnemyClass=%s Point=%d Active=%d Hidden=%d Collision=%d Tick=%d Position=%s"),
		*Enemy.GetName(), *Enemy.GetClass()->GetName(), PointId, Enemy.IsPoolActive(), Enemy.IsHidden(), Enemy.GetActorEnableCollision(), Enemy.IsActorTickEnabled(), *Enemy.GetActorLocation().ToString())); */ {}
	return true;
}
