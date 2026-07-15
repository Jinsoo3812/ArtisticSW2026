#include "Water/SWRippleTypes.h"

float FSWRippleEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	double ServerTime,
	TConstArrayView<FSWRippleEvent> Events)
{
	float TotalHeight = 0.0f;

	for (const FSWRippleEvent& Ripple : Events)
	{
		if (!Ripple.IsActiveAt(ServerTime) || Ripple.WaveLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

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
		const float DistanceFromWavefront = FMath::Abs(Distance - WavefrontRadius);
		const float NormalizedDistance = FMath::Clamp(DistanceFromWavefront / EnvelopeWidth, 0.0f, 1.0f);
		const float Envelope = 1.0f - (NormalizedDistance * NormalizedDistance * (3.0f - 2.0f * NormalizedDistance));
		const float Decay = FMath::Exp(-Ripple.DecayRate * DeltaTime);
		const float Phase = ((Distance - WavefrontRadius) / Ripple.WaveLength) * 2.0f * PI;

		TotalHeight += Ripple.InitialAmplitude * Decay * FMath::Cos(Phase) * Envelope;
	}

	return TotalHeight;
}

