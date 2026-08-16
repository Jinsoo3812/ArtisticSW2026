#include "SWShipWakeTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SWKelvinWakeAtlas.h"

namespace SWShipWake
{
	constexpr float GravityCmPerSecondSquared = 980.0f;
	constexpr float TwoPi = 2.0f * PI;

	struct FTrajectoryCoordinate
	{
		float DownstreamCm = -1.0f;
		float LateralCm = 0.0f;
	};

	FTrajectoryCoordinate MapToTrajectory(
		const FVector2D& QueryPosition,
		const FSWShipWakeEvent& Event,
		const FVector2D& PredictedApex)
	{
		const FVector2D DefaultForward = Event.Forward.IsNearlyZero()
			? FVector2D(1.0f, 0.0f)
			: Event.Forward.GetSafeNormal();
		const int32 PointCount = FMath::Min(Event.TrajectoryPoints.Num(), 16);
		if (PointCount < 2)
		{
			const FVector2D Delta = QueryPosition - PredictedApex;
			return {
				static_cast<float>(-FVector2D::DotProduct(Delta, DefaultForward)),
				static_cast<float>(FVector2D::DotProduct(
					Delta, FVector2D(-DefaultForward.Y, DefaultForward.X)))
			};
		}

		float BestDistanceSquared = TNumericLimits<float>::Max();
		FTrajectoryCoordinate Best;
		float CumulativeDistance = 0.0f;
		FVector2D LastForward = DefaultForward;
		for (int32 Index = 0; Index < PointCount - 1; ++Index)
		{
			const FVector2D A = Index == 0 ? PredictedApex : Event.TrajectoryPoints[Index];
			const FVector2D B = Event.TrajectoryPoints[Index + 1];
			const FVector2D Segment = B - A;
			const float SegmentLength = Segment.Size();
			if (SegmentLength <= 1.0f)
			{
				continue;
			}

			const FVector2D TowardPast = Segment / SegmentLength;
			const FVector2D VesselForward = -TowardPast;
			LastForward = VesselForward;
			const float T = FMath::Clamp(
				FVector2D::DotProduct(QueryPosition - A, Segment) / FMath::Square(SegmentLength),
				0.0f, 1.0f);
			const FVector2D Closest = A + Segment * T;
			const FVector2D Offset = QueryPosition - Closest;
			const float DistanceSquared = Offset.SizeSquared();
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				Best.DownstreamCm = CumulativeDistance + SegmentLength * T;
				Best.LateralCm = FVector2D::DotProduct(
					Offset, FVector2D(-VesselForward.Y, VesselForward.X));
			}
			CumulativeDistance += SegmentLength;
		}

		const FVector2D TailOrigin = Event.TrajectoryPoints[PointCount - 1];
		const FVector2D TailDelta = QueryPosition - TailOrigin;
		const float TailDownstream = FMath::Max(-FVector2D::DotProduct(TailDelta, LastForward), 0.0f);
		const FVector2D TailClosest = TailOrigin - LastForward * TailDownstream;
		const FVector2D TailOffset = QueryPosition - TailClosest;
		if (TailOffset.SizeSquared() < BestDistanceSquared)
		{
			Best.DownstreamCm = CumulativeDistance + TailDownstream;
			Best.LateralCm = FVector2D::DotProduct(
				TailOffset, FVector2D(-LastForward.Y, LastForward.X));
		}

		if (Best.DownstreamCm <= 1.0f
			&& FVector2D::DotProduct(QueryPosition - PredictedApex, DefaultForward) > 0.0f)
		{
			Best.DownstreamCm = -1.0f;
		}
		return Best;
	}

	float EvaluateEventHeight(
		const FVector2D& QueryPosition,
		const double ServerTime,
		const FSWShipWakeEvent& Event)
	{
		if (!Event.IsActiveAt(ServerTime) || !FSWKelvinWakeAtlas::Get().IsReady())
		{
			return 0.0f;
		}

		const FVector2D Forward = Event.Forward.IsNearlyZero()
			? FVector2D(1.0f, 0.0f)
			: Event.Forward.GetSafeNormal();
		const float Age = static_cast<float>(ServerTime - Event.UpdateServerTime);
		const FVector2D PredictedApex = Event.Origin
			+ Forward * FMath::Max(Event.AdvectionSpeedCmPerSecond, 0.0f)
			* FMath::Clamp(Age, 0.0f, 0.20f);
		const FTrajectoryCoordinate Coordinate = MapToTrajectory(QueryPosition, Event, PredictedApex);
		if (Coordinate.DownstreamCm < 0.0f)
		{
			return 0.0f;
		}

		const float Speed = FMath::Max(Event.SpeedCmPerSecond, 1.0f);
		const float PressureSize = FMath::Max(Event.PressureSizeCm, 1.0f);
		const float Froude = Speed / FMath::Sqrt(GravityCmPerSecondSquared * PressureSize);
		const float WavelengthCm = TwoPi * FMath::Square(Speed) / GravityCmPerSecondSquared;
		const float U = Coordinate.DownstreamCm
			/ (WavelengthCm * FMath::Max(Event.LongitudinalScale, 0.01f));
		const float V = Coordinate.LateralCm
			/ (WavelengthCm * FMath::Max(Event.LateralScale, 0.01f));
		const float NormalizedHeight = FSWKelvinWakeAtlas::Get().SampleNormalized(U, V, Froude);
		const float Radius = FMath::Sqrt(
			FMath::Square(Coordinate.DownstreamCm) + FMath::Square(Coordinate.LateralCm));
		const float NearMask = Event.NearHullSuppressDistanceCm <= 0.0f
			? 1.0f
			: FMath::SmoothStep(
				Event.NearHullSuppressDistanceCm,
				Event.NearHullSuppressDistanceCm + PressureSize * 0.25f,
				Radius);
		const float Freshness = 1.0f - FMath::SmoothStep(
			Event.StateLifetime * 0.55f, Event.StateLifetime, Age);
		return Event.Amplitude * NormalizedHeight * NearMask * Freshness;
	}
}

float FSWShipWakeEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_EvaluateHeight_M4Atlas);
	float TotalHeight = 0.0f;
	for (const FSWShipWakeEvent& Event : Events)
	{
		TotalHeight += SWShipWake::EvaluateEventHeight(QueryPosition, ServerTime, Event);
	}
	return FMath::Clamp(TotalHeight, -200.0f, 200.0f);
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
	return FVector2D(
		(EvaluateHeight(QueryPosition + DX, ServerTime, Events)
			- EvaluateHeight(QueryPosition - DX, ServerTime, Events)) / (2.0f * SafeDistance),
		(EvaluateHeight(QueryPosition + DY, ServerTime, Events)
			- EvaluateHeight(QueryPosition - DY, ServerTime, Events)) / (2.0f * SafeDistance));
}
