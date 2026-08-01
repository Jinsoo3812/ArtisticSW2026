#include "Water/SWRippleTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Water/SWRippleProfile.h"

void FSWRippleClientRenderClock::ObserveReplicatedEvent(
	double EventStartServerTime,
	double EstimatedServerTime,
	double LocalWorldTime)
{
	const double CurrentRenderTime = Resolve(EstimatedServerTime, LocalWorldTime);
	if (EventStartServerTime > CurrentRenderTime)
	{
		bHasAnchor = true;
		AnchorServerTime = EventStartServerTime;
		AnchorLocalWorldTime = LocalWorldTime;
	}
}

double FSWRippleClientRenderClock::Resolve(double EstimatedServerTime, double LocalWorldTime) const
{
	if (!bHasAnchor)
	{
		return EstimatedServerTime;
	}

	const double AnchoredTime = AnchorServerTime
		+ FMath::Max(0.0, LocalWorldTime - AnchorLocalWorldTime);
	return FMath::Max(EstimatedServerTime, AnchoredTime);
}

int32 FSWRipplePredictionPolicy::FindBestPredictedEventIndex(
	TConstArrayView<FSWRippleEvent> Events,
	const FSWRippleEvent& AuthoritativeEvent,
	float MaxDistance,
	double MaxTimeDelta)
{
	int32 BestIndex = INDEX_NONE;
	float BestDistanceSquared = FMath::Square(FMath::Max(0.0f, MaxDistance));
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		const FSWRippleEvent& Candidate = Events[Index];
		if (Candidate.EventId >= 0
			|| FMath::Abs(Candidate.StartServerTime - AuthoritativeEvent.StartServerTime) > MaxTimeDelta)
		{
			continue;
		}

		const float DistanceSquared = FVector2D::DistSquared(Candidate.Origin, AuthoritativeEvent.Origin);
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

int32 FSWRippleQueuePolicy::FindOldestEventIndex(TConstArrayView<FSWRippleEvent> Events)
{
	if (Events.IsEmpty())
	{
		return INDEX_NONE;
	}

	int32 OldestIndex = 0;
	for (int32 Index = 1; Index < Events.Num(); ++Index)
	{
		if (Events[Index].EventId < Events[OldestIndex].EventId)
		{
			OldestIndex = Index;
		}
	}
	return OldestIndex;
}

void FSWRippleQueuePolicy::AddOrUpdateCapped(
	TArray<FSWRippleEvent>& Events,
	const FSWRippleEvent& Event,
	int32 MaxEventCount)
{
	if (FSWRippleEvent* Existing = Events.FindByPredicate(
		[&Event](const FSWRippleEvent& Candidate) { return Candidate.EventId == Event.EventId; }))
	{
		*Existing = Event;
		return;
	}

	const int32 SafeMaxEventCount = FMath::Max(1, MaxEventCount);
	while (Events.Num() >= SafeMaxEventCount)
	{
		const int32 OldestIndex = FindOldestEventIndex(Events);
		if (OldestIndex == INDEX_NONE)
		{
			break;
		}
		Events.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
	}
	Events.Add(Event);
}

float FSWRippleEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	double ServerTime,
	TConstArrayView<FSWRippleEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_EvaluateHeight);
	float TotalHeight = 0.0f;
	int32 ActiveEventCount = 0;
	int32 EnvelopeEvaluationCount = 0;

	for (const FSWRippleEvent& Ripple : Events)
	{
		if (!Ripple.IsActiveAt(ServerTime) || Ripple.WaveLength <= UE_SMALL_NUMBER)
		{
			continue;
		}
		++ActiveEventCount;

		const float DeltaTime = static_cast<float>(ServerTime - Ripple.StartServerTime);
		const FVector2D Delta = QueryPosition - Ripple.Origin;
		const float DistanceSquared = Delta.SizeSquared();
		const float WavefrontRadius = Ripple.WaveSpeed * DeltaTime;
		const float EnvelopeWidth = Ripple.WaveLength * 2.0f;
		const float MinRadius = FMath::Max(0.0f, WavefrontRadius - EnvelopeWidth);
		const float MaxRadius = WavefrontRadius + EnvelopeWidth;

		if (DistanceSquared > FMath::Square(MaxRadius)
			|| (MinRadius > 0.0f && DistanceSquared < FMath::Square(MinRadius)))
		{
			continue;
		}

		const float Distance = FMath::Sqrt(DistanceSquared);
		++EnvelopeEvaluationCount;
		const float DistanceFromWavefront = FMath::Abs(Distance - WavefrontRadius);
		const float NormalizedDistance = FMath::Clamp(DistanceFromWavefront / EnvelopeWidth, 0.0f, 1.0f);
		const float Envelope = 1.0f - (NormalizedDistance * NormalizedDistance * (3.0f - 2.0f * NormalizedDistance));
		const float Decay = FMath::Exp(-Ripple.DecayRate * DeltaTime);
		const float Phase = ((Distance - WavefrontRadius) / Ripple.WaveLength) * 2.0f * PI;

		TotalHeight += Ripple.InitialAmplitude * Decay * FMath::Cos(Phase) * Envelope;
	}

	FSWRippleProfile::RecordEvaluation(Events.Num(), ActiveEventCount, EnvelopeEvaluationCount);

	return TotalHeight;
}
