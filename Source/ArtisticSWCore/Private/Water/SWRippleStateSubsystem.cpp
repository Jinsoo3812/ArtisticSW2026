#include "Water/SWRippleStateSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Water/SWRippleProfile.h"
#include "Water/SWRippleReplicator.h"

void USWRippleStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() != NM_Client && !Replicator.IsValid())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("SWRippleReplicator");
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		InWorld.SpawnActor<ASWRippleReplicator>(ASWRippleReplicator::StaticClass(), FTransform::Identity, SpawnParameters);
	}
}

void USWRippleStateSubsystem::Tick(float DeltaTime)
{
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

bool USWRippleStateSubsystem::SubmitAuthoritativeRipple(
	const FVector2D& Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	ASWRippleReplicator* RippleReplicator = Replicator.Get();
	return RippleReplicator
		&& RippleReplicator->AddServerRipple(Origin, InitialAmplitude, WaveSpeed, DecayRate, WaveLength);
}

void USWRippleStateSubsystem::AddOrUpdateReplicatedEvent(const FSWRippleEvent& Event)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_AddOrUpdateEvent);
	{
		FWriteScopeLock WriteLock(EventsLock);
		if (FSWRippleEvent* Existing = Events.FindByPredicate(
			[&Event](const FSWRippleEvent& Candidate) { return Candidate.EventId == Event.EventId; }))
		{
			*Existing = Event;
		}
		else
		{
			Events.Add(Event);
		}
	}

	++Revision;
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
