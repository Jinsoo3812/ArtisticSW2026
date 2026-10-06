#include "Water/SWRippleStateSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Water/SWRippleProfile.h"
#include "Water/SWRippleReplicator.h"
#include "Water/SWRippleSettings.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"

namespace
{
	constexpr float RipplePredictionMatchDistance = 250.0f;
	constexpr double RipplePredictionMatchTime = 1.5;

	double CalculateRippleLifetime(float InitialAmplitude, float DecayRate)
	{
		const float EffectiveDecayRate = FMath::Max(DecayRate, 0.01f);
		return static_cast<double>(FMath::Clamp(
			FMath::Loge(FMath::Max(InitialAmplitude, 0.01f) / 0.01f) / EffectiveDecayRate,
			0.5f,
			10.0f));
	}
}

void USWRippleStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() != NM_Client && !Replicator.IsValid())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("SWRippleReplicator");
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.OverrideLevel = InWorld.PersistentLevel;
		SpawnParameters.bDeferConstruction = true;
		ASWRippleReplicator* Created = InWorld.SpawnActor<ASWRippleReplicator>(ASWRippleReplicator::StaticClass(), FTransform::Identity, SpawnParameters);
		const USWVoyageResetSubsystem* Voyage = InWorld.GetSubsystem<USWVoyageResetSubsystem>();
		FString Error;
		if (Created && (!FSWVoyageSpawn::RegisterDeferredActorSpawn(Created, ESWVoyageActorLifetime::SharedService,
			Voyage ? Voyage->GetGeneration() : 0, FGuid(), Error)
			|| USWVoyageSpawnLibrary::FinishVoyageActorSpawn(Created, FTransform::Identity) != Created))
		{
			UE_LOG(LogTemp, Error, TEXT("Ripple replicator creation failed: %s"), *Error);
			if (IsValid(Created)) Created->Destroy();
		}
	}
}

void USWRippleStateSubsystem::Tick(float DeltaTime)
{
	AcceptCurrentVoyageGeneration();
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_StateTick);
	const double PruneBefore = GetServerTime() - static_cast<double>(PhysicsHistoryRetentionSeconds);
	bool bRemoved = false;
	int32 StoredEventCount = 0;
	{
		FWriteScopeLock WriteLock(EventsLock);
		for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
		{
			if (Events[Index].ExpireServerTime < PruneBefore)
			{
				Events.RemoveAtSwap(Index, 1, EAllowShrinking::No);
				bRemoved = true;
			}
		}
		StoredEventCount = Events.Num();
	}

	if (bRemoved)
	{
		++Revision;
	}
	FSWRippleProfile::RecordState(StoredEventCount, Revision.Load());
}

TStatId USWRippleStateSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWRippleStateSubsystem, STATGROUP_Tickables);
}

void USWRippleStateSubsystem::AcceptCurrentVoyageGeneration()
{
	check(IsInGameThread());
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	const int32 Generation = Voyage ? Voyage->GetGeneration() : 0;
	if (AcceptedGeneration == Generation) return;
	AcceptedGeneration = Generation;
	{
		FWriteScopeLock Lock(EventsLock);
		Events.Reset();
	}
	++Revision; NextPredictedEventId = -1;
	ClientRenderClock = FSWRippleClientRenderClock();
	if (Replicator.IsValid() && Replicator->HasAuthority()) Replicator->ResetForVoyage(Generation);
	TArray<FSWRippleEvent> Pending = MoveTemp(FutureEvents);
	for (const FSWRippleEvent& Event : Pending)
	{
		if (Event.Generation == Generation) AddOrUpdateReplicatedEvent(Event);
		else if (Event.Generation > Generation) FutureEvents.Add(Event);
	}
}

FName USWRippleStateSubsystem::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<USWRippleStateSubsystem*>(this)) : NAME_None;
}

ESWVoyageStepResult USWRippleStateSubsystem::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult USWRippleStateSubsystem::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	AcceptCurrentVoyageGeneration();
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult USWRippleStateSubsystem::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	AcceptCurrentVoyageGeneration();
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult USWRippleStateSubsystem::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (AcceptedGeneration != Context.Generation)
	{
		OutError = TEXT("RippleStateGenerationMismatch");
		return ESWVoyageStepResult::Failed;
	}
	return Replicator.IsValid() ? ESWVoyageStepResult::Succeeded : ESWVoyageStepResult::Pending;
}

bool USWRippleStateSubsystem::SubmitAuthoritativeRipple(
	const FVector2D& Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return false;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	ASWRippleReplicator* RippleReplicator = Replicator.Get();
	return RippleReplicator
		&& RippleReplicator->AddServerRipple(Origin, InitialAmplitude, WaveSpeed, DecayRate, WaveLength);
}

bool USWRippleStateSubsystem::SubmitPredictedRipple(
	const FVector2D& Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return false;
	AcceptCurrentVoyageGeneration();
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Client
		|| InitialAmplitude <= 0.0f || WaveLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	FSWRippleEvent PredictedEvent;
	PredictedEvent.EventId = NextPredictedEventId--;
	PredictedEvent.Generation = AcceptedGeneration == INDEX_NONE ? 0 : AcceptedGeneration;
	PredictedEvent.Origin = Origin;
	PredictedEvent.StartServerTime = GetServerTime();
	PredictedEvent.InitialAmplitude = InitialAmplitude;
	PredictedEvent.WaveSpeed = WaveSpeed;
	PredictedEvent.DecayRate = DecayRate;
	PredictedEvent.WaveLength = WaveLength;
	PredictedEvent.ExpireServerTime = PredictedEvent.StartServerTime
		+ CalculateRippleLifetime(InitialAmplitude, DecayRate);

	{
		FWriteScopeLock WriteLock(EventsLock);
		for (const FSWRippleEvent& Existing : Events)
		{
			if (Existing.EventId > 0
				&& FVector2D::DistSquared(Existing.Origin, Origin)
					<= FMath::Square(RipplePredictionMatchDistance)
				&& FMath::Abs(Existing.StartServerTime - PredictedEvent.StartServerTime)
					<= RipplePredictionMatchTime)
			{
				return true;
			}
		}

		FSWRippleQueuePolicy::AddOrUpdateCapped(
			Events,
			PredictedEvent,
			GetDefault<USWRippleSettings>()->GetMaxRippleCount());
	}

	++Revision;
	if (FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics")))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RIPPLE-LATENCY][Client] PredictionCreated Id=%d Start=%.6f Origin=%s"),
			PredictedEvent.EventId,
			PredictedEvent.StartServerTime,
			*PredictedEvent.Origin.ToString());
	}
	return true;
}

void USWRippleStateSubsystem::AddOrUpdateReplicatedEvent(const FSWRippleEvent& Event)
{
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	const int32 Generation = Voyage ? Voyage->GetGeneration() : 0;
	if (Event.Generation < Generation) return;
	if (Event.Generation > Generation)
	{
		const int32 ExistingIndex = FutureEvents.IndexOfByPredicate([&Event](const FSWRippleEvent& Other)
			{ return Other.Generation == Event.Generation && Other.EventId == Event.EventId; });
		if (ExistingIndex != INDEX_NONE) FutureEvents[ExistingIndex] = Event;
		else
		{
			const int32 Capacity = GetDefault<USWRippleSettings>()->GetMaxRippleCount();
			if (FutureEvents.Num() >= Capacity) FutureEvents.RemoveAt(0, 1, EAllowShrinking::No);
			FutureEvents.Add(Event);
		}
		return;
	}
	AcceptCurrentVoyageGeneration();
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_AddOrUpdateEvent);
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		const double EstimatedServerTime = GetEstimatedServerTime();
		const double LocalWorldTime = World->GetTimeSeconds();
		const double ClockLeadBefore = Event.StartServerTime - GetServerTime();
		ClientRenderClock.ObserveReplicatedEvent(
			Event.StartServerTime,
			EstimatedServerTime,
			LocalWorldTime);

		if (FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics")))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[RIPPLE-LATENCY][Client] EventReceived Id=%d Start=%.6f Estimated=%.6f Render=%.6f StartMinusRenderBeforeMs=%.1f"),
				Event.EventId,
				Event.StartServerTime,
				EstimatedServerTime,
				GetServerTime(),
				ClockLeadBefore * 1000.0);
		}
	}

	int32 ReconciledPredictionId = 0;
	double PredictionStartTime = 0.0;
	FSWRippleEvent EventToStore = Event;
	{
		FWriteScopeLock WriteLock(EventsLock);
		if (World && World->GetNetMode() == NM_Client && Event.EventId > 0)
		{
			const FSWRippleEvent* ExistingAuthority = Events.FindByPredicate(
				[&Event](const FSWRippleEvent& Existing)
				{
					return Existing.EventId == Event.EventId;
				});
			if (ExistingAuthority)
			{
				const double AuthoritativeLifetime = Event.ExpireServerTime - Event.StartServerTime;
				EventToStore.StartServerTime = ExistingAuthority->StartServerTime;
				EventToStore.ExpireServerTime = ExistingAuthority->StartServerTime + AuthoritativeLifetime;
			}
			else
			{
				const int32 PredictedIndex = FSWRipplePredictionPolicy::FindBestPredictedEventIndex(
					Events,
					Event,
					RipplePredictionMatchDistance,
					RipplePredictionMatchTime);
				if (PredictedIndex != INDEX_NONE)
				{
					const FSWRippleEvent& Prediction = Events[PredictedIndex];
					ReconciledPredictionId = Prediction.EventId;
					PredictionStartTime = Prediction.StartServerTime;
					const double AuthoritativeLifetime = Event.ExpireServerTime - Event.StartServerTime;
					EventToStore.StartServerTime = Prediction.StartServerTime;
					EventToStore.ExpireServerTime = Prediction.StartServerTime + AuthoritativeLifetime;
					Events.RemoveAtSwap(PredictedIndex, 1, EAllowShrinking::No);
				}
			}
		}
		FSWRippleQueuePolicy::AddOrUpdateCapped(
			Events,
			EventToStore,
			GetDefault<USWRippleSettings>()->GetMaxRippleCount());
	}

	++Revision;
	if (ReconciledPredictionId != 0
		&& FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics")))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RIPPLE-LATENCY][Client] PredictionReconciled PredictedId=%d AuthorityId=%d VisualStart=%.6f AuthorityStart=%.6f DeltaMs=%.1f"),
			ReconciledPredictionId,
			Event.EventId,
			PredictionStartTime,
			Event.StartServerTime,
			(PredictionStartTime - Event.StartServerTime) * 1000.0);
	}
}

void USWRippleStateSubsystem::GetEventsSnapshot(TArray<FSWRippleEvent>& OutEvents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_GetFullSnapshot);
	const uint64 ProfileStartCycles = FSWRippleProfile::IsEnabled() ? FPlatformTime::Cycles64() : 0;
	{
		FReadScopeLock ReadLock(EventsLock);
		OutEvents = Events;
	}
	FSWRippleProfile::RecordFullSnapshot(OutEvents.Num(), FPlatformTime::Cycles64() - ProfileStartCycles);
}

void USWRippleStateSubsystem::GetActiveEventsSnapshot(double ServerTime, TArray<FSWRippleEvent>& OutEvents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_GetActiveSnapshot);
	const uint64 ProfileStartCycles = FSWRippleProfile::IsEnabled() ? FPlatformTime::Cycles64() : 0;
	OutEvents.Reset();
	{
		FReadScopeLock ReadLock(EventsLock);
		for (const FSWRippleEvent& Event : Events)
		{
			if (Event.IsActiveAt(ServerTime))
			{
				OutEvents.Add(Event);
			}
		}
	}
	FSWRippleProfile::RecordActiveSnapshot(OutEvents.Num(), FPlatformTime::Cycles64() - ProfileStartCycles);
}

float USWRippleStateSubsystem::GetRippleHeight(const FVector& Location, double ServerTime) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_GetHeight);
	FReadScopeLock ReadLock(EventsLock);
	return FSWRippleEvaluator::EvaluateHeight(FVector2D(Location.X, Location.Y), ServerTime, Events);
}

int32 USWRippleStateSubsystem::GetEventCount() const
{
	FReadScopeLock ReadLock(EventsLock);
	return Events.Num();
}

void USWRippleStateSubsystem::RegisterReplicator(ASWRippleReplicator* InReplicator)
{
	if (InReplicator)
	{
		Replicator = InReplicator;
	}
}

double USWRippleStateSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const double EstimatedServerTime = GetEstimatedServerTime();
	if (World && World->GetNetMode() == NM_Client)
	{
		return ClientRenderClock.Resolve(EstimatedServerTime, World->GetTimeSeconds());
	}
	return EstimatedServerTime;
}

double USWRippleStateSubsystem::GetEstimatedServerTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0;
}
