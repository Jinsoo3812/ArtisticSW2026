#include "Water/SWRippleTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Water/SWRippleProfile.h"

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
