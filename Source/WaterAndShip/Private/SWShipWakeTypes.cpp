#include "SWShipWakeTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SWKelvinWakeAtlas.h"

namespace
{
	bool EvaluateGoldenSample(
		const FVector2D& QueryPosition,
		const double ServerTime,
		const FSWShipWakeEvent& Event,
		float& OutWeightedHeight,
		float& OutBlendWeight)
	{
		OutWeightedHeight = 0.0f;
		OutBlendWeight = 0.0f;
		if (!Event.IsActiveAt(ServerTime) || !FSWKelvinWakeAtlas::Get().IsReady()) return false;

		const float Age = static_cast<float>(ServerTime - Event.StartServerTime);
		const float Decay = FMath::Exp(-FMath::Max(Event.DecayRate, 0.0f) * Age);
		if (Decay < 0.001f) return false;

		const FVector2D Forward = Event.Forward.IsNearlyZero()
			? FVector2D(1.0, 0.0) : Event.Forward.GetSafeNormal();
		const FVector2D Right(-Forward.Y, Forward.X);
		const FVector2D Delta = QueryPosition - Event.Origin;
		const float Downstream = -FVector2D::DotProduct(Delta, Forward);
		const float Lateral = FVector2D::DotProduct(Delta, Right);

		const float Radius = FMath::Sqrt(Downstream * Downstream + Lateral * Lateral);
		const float Front = Event.PropagationSpeedCmPerSecond * Age;
		const float EnvelopeWidth = FMath::Max(Event.EnvelopeWidthCm, 1.0f);
		const float FrontDistance = FMath::Abs(Radius - Front);
		if (FrontDistance >= EnvelopeWidth) return false;

		const float FrontEnvelope = 1.0f - FMath::SmoothStep(0.0f, EnvelopeWidth, FrontDistance);
		const float FadeIn = Event.FadeInSeconds > UE_SMALL_NUMBER
			? FMath::SmoothStep(0.0f, Event.FadeInSeconds, Age) : 1.0f;
		OutBlendWeight = FrontEnvelope * FadeIn;
		if (OutBlendWeight <= 0.0f) return false;

		const float Length = FMath::Max(Event.WakeLengthCm, 1.0f);
		const float HalfWidth = FMath::Max(Event.WakeHalfWidthCm, 1.0f);
		const float LengthCut = FMath::Clamp(Event.LengthCutRatio, 0.01f, 1.0f);
		const float WidthCut = FMath::Clamp(Event.WidthCutRatio, 0.01f, 1.0f);

		const float U = Downstream / Length;
		const float V_signed = Lateral / HalfWidth;

		float Golden = 0.0f;
		if (U >= 0.0f && U <= LengthCut && FMath::Abs(V_signed) <= WidthCut)
		{
			const float UFadeIn = FMath::SmoothStep(0.0f, 0.03f, U);
			const float UFadeOut = 1.0f - FMath::SmoothStep(LengthCut * 0.70f, LengthCut, U);
			const float VNorm = FMath::Abs(V_signed) / WidthCut;
			const float VFade = 1.0f - FMath::SmoothStep(0.60f, 1.0f, VNorm);
			const float StampMask = UFadeIn * UFadeOut * VFade;

			if (StampMask > 0.0001f)
			{
				Golden = FSWKelvinWakeAtlas::Get().SampleFixedNormalized(
					U, V_signed, Event.FroudeProfile) * StampMask;
			}
		}

		OutWeightedHeight = Event.InitialAmplitudeCm * Golden * OutBlendWeight * Decay;
		return true;
	}
}

float FSWShipWakeEvaluator::EvaluateEventHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	const FSWShipWakeEvent& Event)
{
	float Height = 0.0f;
	float Weight = 0.0f;
	EvaluateGoldenSample(QueryPosition, ServerTime, Event, Height, Weight);
	return Height;
}

float FSWShipWakeEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_M7_EvaluateHeight);
	float WeightedHeight = 0.0f;
	float BlendWeight = 0.0f;
	for (const FSWShipWakeEvent& Event : Events)
	{
		float EventHeight = 0.0f;
		float EventWeight = 0.0f;
		if (EvaluateGoldenSample(QueryPosition, ServerTime, Event, EventHeight, EventWeight))
		{
			WeightedHeight += EventHeight;
			BlendWeight += EventWeight;
		}
	}
	// Golden is already a complete wake solution. Normalized overlap reconstructs
	// one continuous moving source instead of summing many complete wakes.
	return FMath::Clamp(WeightedHeight / FMath::Max(BlendWeight, 1.0f), -200.0f, 200.0f);
}

FSWShipWakeDebugSample FSWShipWakeEvaluator::EvaluateDebug(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events,
	const bool bIncludeEventDetails)
{
	FSWShipWakeDebugSample Sample;
	Sample.TotalEventsChecked = Events.Num();
	TArray<FString> ContribDetails;

	for (const FSWShipWakeEvent& Event : Events)
	{
		float EventHeight = 0.0f;
		float EventWeight = 0.0f;
		if (EvaluateGoldenSample(QueryPosition, ServerTime, Event, EventHeight, EventWeight))
		{
			Sample.WeightedHeight += EventHeight;
			Sample.BlendWeight += EventWeight;
			++Sample.ActiveContributingEvents;

			if (bIncludeEventDetails)
			{
				const float Age = static_cast<float>(ServerTime - Event.StartServerTime);
				ContribDetails.Add(FString::Printf(
					TEXT("[E%d: Age=%.2fs, W=%.2f, H=%.1fcm]"),
					Event.EventId, Age, EventWeight, EventHeight));
			}
		}
	}
	Sample.FinalHeight = FMath::Clamp(
		Sample.WeightedHeight / FMath::Max(Sample.BlendWeight, 1.0f), -200.0f, 200.0f);
	Sample.DetailLog = FString::Join(ContribDetails, TEXT(" "));
	return Sample;
}

FVector2D FSWShipWakeEvaluator::EvaluateGradient(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events,
	const float SampleDistance)
{
	const float D = FMath::Max(SampleDistance, 1.0f);
	return FVector2D(
		(EvaluateHeight(QueryPosition + FVector2D(D, 0.0), ServerTime, Events)
			- EvaluateHeight(QueryPosition - FVector2D(D, 0.0), ServerTime, Events)) / (2.0f * D),
		(EvaluateHeight(QueryPosition + FVector2D(0.0, D), ServerTime, Events)
			- EvaluateHeight(QueryPosition - FVector2D(0.0, D), ServerTime, Events)) / (2.0f * D));
}
