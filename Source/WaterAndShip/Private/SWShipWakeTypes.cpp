#include "SWShipWakeTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SWShipWake
{
	constexpr float MinimumAngularWidth = 0.035f;
	constexpr float MinimumEnvelopeWidth = 1.0f;

	float EvaluateEventHeight(
		const FVector2D& QueryPosition,
		const double ServerTime,
		const FSWShipWakeEvent& Event)
	{
		if (!Event.IsActiveAt(ServerTime))
		{
			return 0.0f;
		}

		const FVector2D Forward = Event.Forward.IsNearlyZero()
			? FVector2D(1.0, 0.0)
			: Event.Forward.GetSafeNormal();
		const FVector2D Right(-Forward.Y, Forward.X);
		const FVector2D Delta = QueryPosition - Event.Origin;
		const float Behind = -FVector2D::DotProduct(Delta, Forward);
		if (Behind <= 0.0f)
		{
			return 0.0f;
		}

		const float Lateral = FMath::Abs(FVector2D::DotProduct(Delta, Right));
		const float RadiusSquared = Behind * Behind + Lateral * Lateral;
		const float Age = static_cast<float>(ServerTime - Event.StartServerTime);
		const float WaveLength = FMath::Max(Event.WaveLength, 1.0f);
		const float WavefrontRadius = FMath::Max(Event.PhaseSpeed, 0.0f) * Age;
		const float EnvelopeWidth = FMath::Max(WaveLength * 2.75f, MinimumEnvelopeWidth);
		const float MinimumRadius = FMath::Max(0.0f, WavefrontRadius - EnvelopeWidth);
		const float MaximumRadius = WavefrontRadius + EnvelopeWidth;
		if (RadiusSquared < FMath::Square(MinimumRadius)
			|| RadiusSquared > FMath::Square(MaximumRadius))
		{
			return 0.0f;
		}

		const float Radius = FMath::Sqrt(RadiusSquared);
		const float Angle = FMath::Atan2(Lateral, FMath::Max(Behind, 1.0f));
		const float HalfAngle = FMath::Clamp(Event.KelvinHalfAngleRadians, 0.1f, 0.7f);
		const float WedgeEdge = HalfAngle + 0.12f;
		if (Angle >= WedgeEdge)
		{
			return 0.0f;
		}

		const float WedgeMask = 1.0f - FMath::SmoothStep(HalfAngle, WedgeEdge, Angle);
		const float RadialOffset = Radius - WavefrontRadius;
		const float NormalizedEnvelope = FMath::Clamp(FMath::Abs(RadialOffset) / EnvelopeWidth, 0.0f, 1.0f);
		const float RadialEnvelope = 1.0f - FMath::SmoothStep(0.45f, 1.0f, NormalizedEnvelope);

		const float DivergentAngularWidth = FMath::Max(HalfAngle * 0.24f, MinimumAngularWidth);
		const float DivergentAngularOffset = (Angle - HalfAngle) / DivergentAngularWidth;
		const float DivergentMask = FMath::Exp(-DivergentAngularOffset * DivergentAngularOffset);

		const float TransverseAngularScale = FMath::Max(HalfAngle * 0.72f, MinimumAngularWidth);
		const float TransverseRatio = Angle / TransverseAngularScale;
		const float TransverseMask = FMath::Exp(-FMath::Square(FMath::Square(TransverseRatio)));

		const float Phase = (RadialOffset / WaveLength) * 2.0f * PI;
		const float DivergentWave = FMath::Cos(Phase) * DivergentMask;
		const float TransverseWave = FMath::Cos(Phase * 0.72f + 0.65f) * TransverseMask * 0.42f;
		const float TimeDecay = FMath::Exp(-Age / FMath::Max(Event.Lifetime * 0.72f, 0.1f));
		const float DistanceDecay = FMath::InvSqrt(1.0f + Radius / (WaveLength * 2.0f));

		return Event.InitialAmplitude
			* (DivergentWave + TransverseWave)
			* WedgeMask
			* RadialEnvelope
			* TimeDecay
			* DistanceDecay;
	}
}

float FSWShipWakeEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_EvaluateHeight);
	float TotalHeight = 0.0f;
	for (const FSWShipWakeEvent& Event : Events)
	{
		TotalHeight += SWShipWake::EvaluateEventHeight(QueryPosition, ServerTime, Event);
	}
	// Keep the shared M1 field inside the allowance advertised by
	// USWRippleWaterWaves::GetMaxWaveHeight and avoid pathological overlap spikes.
	return FMath::Clamp(TotalHeight, -100.0f, 100.0f);
}

FVector2D FSWShipWakeEvaluator::EvaluateGradient(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events,
	const float SampleDistance)
{
	const float SafeDistance = FMath::Max(SampleDistance, 1.0f);
	const FVector2D DX(SafeDistance, 0.0f);
	const FVector2D DY(0.0f, SafeDistance);
	const float GradientX = (
		EvaluateHeight(QueryPosition + DX, ServerTime, Events)
		- EvaluateHeight(QueryPosition - DX, ServerTime, Events)) / (2.0f * SafeDistance);
	const float GradientY = (
		EvaluateHeight(QueryPosition + DY, ServerTime, Events)
		- EvaluateHeight(QueryPosition - DY, ServerTime, Events)) / (2.0f * SafeDistance);
	return FVector2D(GradientX, GradientY);
}
