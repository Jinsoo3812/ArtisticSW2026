#include "SWShipWakeTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SWShipWake
{
	constexpr float GravityCmPerSecondSquared = 980.0f;
	constexpr int32 DirectionSampleCount = 16;
	constexpr float MaximumWaveVectorAngle = 1.22173048f; // 70 degrees.
	constexpr float KelvinHalfAngleTangent = 0.35355339f; // tan(asin(1/3)).

	float EvaluateSourceHeight(
		const FVector2D& QueryPosition,
		const FSWShipWakeEvent& Event,
		const FVector2D& SourceOrigin,
		const float SourceStrength,
		const float SourcePhaseOffset)
	{
		if (SourceStrength <= UE_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const FVector2D Forward = Event.Forward.IsNearlyZero()
			? FVector2D(1.0, 0.0)
			: Event.Forward.GetSafeNormal();
		const FVector2D Right(-Forward.Y, Forward.X);
		const FVector2D Delta = QueryPosition - SourceOrigin;
		const float Downstream = -FVector2D::DotProduct(Delta, Forward);
		if (Downstream <= 0.0f || Downstream >= Event.WakeLengthCm)
		{
			return 0.0f;
		}

		const float Lateral = FVector2D::DotProduct(Delta, Right);
		const float AbsoluteLateral = FMath::Abs(Lateral);
		const float HullLength = FMath::Max(Event.HullLengthCm, 100.0f);
		const float Beam = FMath::Max(Event.BeamWidthCm, 50.0f);
		const float Draft = FMath::Max(Event.DraftCm, 1.0f);
		const float Speed = FMath::Max(Event.SpeedCmPerSecond, 1.0f);

		// The integral creates the internal wave families. This soft support mask
		// only removes finite-sample leakage outside Kelvin's cusp envelope.
		const float LateralRatio = AbsoluteLateral / FMath::Max(Downstream, 1.0f);
		const float EdgeBlend = FMath::Clamp(Beam / HullLength * 0.45f, 0.025f, 0.12f);
		const float WedgeMask = 1.0f - FMath::SmoothStep(
			KelvinHalfAngleTangent,
			KelvinHalfAngleTangent + EdgeBlend,
			LateralRatio);
		if (WedgeMask <= UE_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const float Radius = FMath::Sqrt(Downstream * Downstream + Lateral * Lateral);
		const float SuppressRadius = FMath::Max(Beam * 0.35f, 25.0f);
		const float NearFieldMask = FMath::SmoothStep(SuppressRadius, SuppressRadius + Beam, Radius);
		const float DistanceFade = 1.0f - FMath::SmoothStep(
			Event.WakeLengthCm * 0.72f,
			Event.WakeLengthCm,
			Downstream);
		const float GeometricDecay = FMath::InvSqrt(1.0f + Downstream / (HullLength * 3.0f));

		float DirectionalSum = 0.0f;
		for (int32 DirectionIndex = 0; DirectionIndex < DirectionSampleCount; ++DirectionIndex)
		{
			const float DirectionAlpha = (static_cast<float>(DirectionIndex) + 0.5f)
				/ static_cast<float>(DirectionSampleCount);
			const float Theta = FMath::Lerp(-MaximumWaveVectorAngle, MaximumWaveVectorAngle, DirectionAlpha);
			const float CosTheta = FMath::Cos(Theta);
			const float SinTheta = FMath::Sin(Theta);
			const float WaveNumber = GravityCmPerSecondSquared
				/ FMath::Max(Speed * Speed * CosTheta * CosTheta, 1.0f);

			const float SpectrumAlpha = FMath::Abs(Theta) / MaximumWaveVectorAngle;
			const float SpectrumBlend = FMath::SmoothStep(0.22f, 0.78f, SpectrumAlpha);
			const float SpectrumMagnitude = FMath::Lerp(
				Event.TransverseStrength,
				Event.DivergentStrength,
				SpectrumBlend);
			// A compact hull-pressure spectrum: draft damps short waves and beam
			// limits the high-angle/high-wave-number tail.
			const float DraftFilter = FMath::Exp(-WaveNumber * Draft * 0.35f);
			const float BeamFilter = FMath::InvSqrt(
				1.0f + FMath::Square(WaveNumber * Beam * 0.40f));
			const float QuadratureWeight = SpectrumMagnitude * DraftFilter * BeamFilter * CosTheta;
			const float Phase = WaveNumber
				* (-Downstream * CosTheta + Lateral * SinTheta)
				+ SourcePhaseOffset;
			DirectionalSum += QuadratureWeight * FMath::Cos(Phase);
		}

		const float NormalizedSpectrum = DirectionalSum * (2.0f / static_cast<float>(DirectionSampleCount));
		return Event.Amplitude
			* SourceStrength
			* NormalizedSpectrum
			* WedgeMask
			* NearFieldMask
			* DistanceFade
			* GeometricDecay;
	}

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
		const float Age = static_cast<float>(ServerTime - Event.UpdateServerTime);
		const float PredictionTime = FMath::Clamp(Age, 0.0f, 0.20f);
		const FVector2D SternOrigin = Event.Origin
			+ Forward * FMath::Max(Event.AdvectionSpeedCmPerSecond, 0.0f) * PredictionTime;
		const FVector2D BowOrigin = SternOrigin + Forward * FMath::Max(Event.HullLengthCm, 100.0f);
		const float Freshness = 1.0f - FMath::SmoothStep(
			Event.StateLifetime * 0.55f,
			Event.StateLifetime,
			Age);

		const float BowHeight = EvaluateSourceHeight(
			QueryPosition, Event, BowOrigin, 1.0f, 0.0f);
		const float SternHeight = EvaluateSourceHeight(
			QueryPosition, Event, SternOrigin, Event.SternStrength, Event.SternPhaseOffsetRadians);
		return (BowHeight + SternHeight) * Freshness;
	}
}

float FSWShipWakeEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_EvaluateHeight_M2);
	float TotalHeight = 0.0f;
	for (const FSWShipWakeEvent& Event : Events)
	{
		TotalHeight += SWShipWake::EvaluateEventHeight(QueryPosition, ServerTime, Event);
	}
	return FMath::Clamp(TotalHeight, -150.0f, 150.0f);
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
