#include "SWShipWakeTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SWKelvinWakeAtlas.h"

namespace
{
	bool EvaluateGoldenSample(
		const FVector2D& QueryPosition,
		const double ServerTime,
		const FSWShipWakeEvent& Event,
		float& OutHeight,
		FSWShipWakeEventDiagnosticSample* Diagnostic = nullptr)
	{
		OutHeight = 0.0f;
		if (!Event.IsActiveAt(ServerTime) || !FSWKelvinWakeAtlas::Get().IsReady()) return false;

		const FVector2D P0 = Event.Origin;
		const FVector2D P1 = Event.EndOrigin.IsNearlyZero() && Event.EndServerTime <= Event.StartServerTime
			? Event.Origin : Event.EndOrigin;
		const double T0 = Event.StartServerTime;
		const double T1 = FMath::Max(Event.EndServerTime, T0);
		const float C = FMath::Max(Event.PropagationSpeedCmPerSecond, 1.0f);

		if (ServerTime < T0 || ServerTime >= Event.ExpireServerTime) return false;

		const float DeltaT = FMath::Max(static_cast<float>(T1 - T0), 1.0e-4f);
		const FVector2D V = (P1 - P0) / DeltaT;
		const float V2 = static_cast<float>(FVector2D::DotProduct(V, V));
		const float C2 = C * C;
		const FVector2D D = P0 - QueryPosition;
		const float Trem = static_cast<float>(ServerTime - T0);

		const float A = V2 - C2;
		const float B = 2.0f * (static_cast<float>(FVector2D::DotProduct(D, V)) + C2 * Trem);
		const float C_coeff = static_cast<float>(FVector2D::DotProduct(D, D)) - C2 * Trem * Trem;

		// Snap only floating-point roundoff at either boundary. The end remains
		// exclusive so adjacent segments never both own the same emission time.
		// Keep this policy identical to SWShipWake.ush.
		const float RootTolerance = FMath::Min(0.25f * DeltaT,
			8.0f * FLT_EPSILON * FMath::Max(1.0f, FMath::Max(Trem, DeltaT)));
		const auto NormalizeRoot = [DeltaT, RootTolerance](float Root)
		{
			if (FMath::Abs(Root) <= RootTolerance) return 0.0f;
			if (FMath::Abs(Root - DeltaT) <= RootTolerance) return DeltaT;
			return Root;
		};
		float Tau = -1.0f;
		if (FMath::Abs(A) < 1.0e-5f)
		{
			if (FMath::Abs(B) > 1.0e-5f)
			{
				const float LinearTau = NormalizeRoot(-C_coeff / B);
				if (LinearTau >= 0.0f && LinearTau < DeltaT && LinearTau <= Trem)
				{
					Tau = LinearTau;
				}
			}
		}
		else
		{
			const float Disc = B * B - 4.0f * A * C_coeff;
			if (Disc >= 0.0f)
			{
				const float SqrtDisc = FMath::Sqrt(Disc);
				const float Tau1 = NormalizeRoot((-B - SqrtDisc) / (2.0f * A));
				const float Tau2 = NormalizeRoot((-B + SqrtDisc) / (2.0f * A));
				const bool bValid1 = (Tau1 >= 0.0f && Tau1 < DeltaT && Tau1 <= Trem);
				const bool bValid2 = (Tau2 >= 0.0f && Tau2 < DeltaT && Tau2 <= Trem);
				if (bValid1)
				{
					Tau = Tau1;
				}
				else if (bValid2)
				{
					Tau = Tau2;
				}
			}
		}

		if (Tau < 0.0f) return false;

		const float Alpha = FMath::Clamp(Tau / DeltaT, 0.0f, 1.0f);
		// 렌더링과 같은 출발 시각 Alpha로 진폭을 보간한다. 쿼리와 비동기 부력도 이 함수를 공유한다.
		// 현재 프레임 진폭으로 과거 wake를 덮어쓰지 않고, 기록된 구간 양 끝 진폭을 사용한다.
		const float Amp = FMath::Lerp(Event.InitialAmplitudeCm, Event.GetEndAmplitudeCm(), Alpha);
		const float Age = Trem - Tau;
		if (Age < 0.0f) return false;
		if (Diagnostic)
		{
			Diagnostic->bHasRoot = true;
			Diagnostic->Alpha = Alpha;
			Diagnostic->Age = Age;
			Diagnostic->AmplitudeCm = Amp;
		}

		const float DecayRate = FMath::Max(Event.DecayRate, 0.0f);
		const float Decay = FMath::Exp(-DecayRate * Age);
		if (Decay < 0.001f) return false;

		const FVector2D Apex = FMath::Lerp(P0, P1, Alpha);
		const FVector2D Fwd0 = Event.Forward.IsNearlyZero() ? FVector2D(1.0, 0.0) : Event.Forward.GetSafeNormal();
		const FVector2D Fwd1 = Event.EndForward.IsNearlyZero() ? Fwd0 : Event.EndForward.GetSafeNormal();
		const FVector2D Forward = FMath::Lerp(Fwd0, Fwd1, Alpha).GetSafeNormal();
		const FVector2D Right(-Forward.Y, Forward.X);

		const FVector2D Delta = QueryPosition - Apex;
		const float Downstream = -static_cast<float>(FVector2D::DotProduct(Delta, Forward));
		const float Lateral = static_cast<float>(FVector2D::DotProduct(Delta, Right));

		const float Length = FMath::Max(Event.WakeLengthCm, 1.0f);
		const float HalfWidth = FMath::Max(Event.WakeHalfWidthCm, 1.0f);
		const float LengthCut = FMath::Clamp(Event.LengthCutRatio, 0.01f, 1.0f);
		const float WidthCut = 1.0f;

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
			if (Diagnostic) Diagnostic->StampMask = StampMask;

			if (StampMask > 0.0001f)
			{
				Golden = FSWKelvinWakeAtlas::Get().SampleFixedNormalized(
					U, V_signed, Event.FroudeProfile) * StampMask;
			}
		}

		const float FadeIn = Event.FadeInSeconds > UE_SMALL_NUMBER
			? FMath::SmoothStep(0.0f, Event.FadeInSeconds, Age) : 1.0f;

		OutHeight = Amp * Golden * FadeIn * Decay;
		if (Diagnostic)
		{
			Diagnostic->FadeIn = FadeIn;
			Diagnostic->Height = OutHeight;
		}
		return true;
	}
}

FSWShipWakeEventDiagnosticSample FSWShipWakeEvaluator::EvaluateEventDiagnostic(
	const FVector2D& QueryPosition, const double ServerTime, const FSWShipWakeEvent& Event)
{
	FSWShipWakeEventDiagnosticSample Sample;
	float Height = 0.0f;
	EvaluateGoldenSample(QueryPosition, ServerTime, Event, Height, &Sample);
	return Sample;
}

float FSWShipWakeEvaluator::EvaluateEventHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	const FSWShipWakeEvent& Event)
{
	float Height = 0.0f;
	EvaluateGoldenSample(QueryPosition, ServerTime, Event, Height);
	return Height;
}

float FSWShipWakeEvaluator::EvaluateHeight(
	const FVector2D& QueryPosition,
	const double ServerTime,
	TConstArrayView<FSWShipWakeEvent> Events)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_M7_EvaluateHeight);
	float TotalHeight = 0.0f;
	for (const FSWShipWakeEvent& Event : Events)
	{
		float EventHeight = 0.0f;
		if (EvaluateGoldenSample(QueryPosition, ServerTime, Event, EventHeight))
		{
			TotalHeight += EventHeight;
		}
	}
	return FMath::Clamp(TotalHeight, -200.0f, 200.0f);
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
		if (EvaluateGoldenSample(QueryPosition, ServerTime, Event, EventHeight))
		{
			Sample.WeightedHeight += EventHeight;
			Sample.BlendWeight += 1.0f;
			++Sample.ActiveContributingEvents;

			if (bIncludeEventDetails)
			{
				const float Age = static_cast<float>(ServerTime - Event.StartServerTime);
				ContribDetails.Add(FString::Printf(
					TEXT("[E%d: Age=%.2fs, H=%.1fcm]"),
					Event.EventId, Age, EventHeight));
			}
		}
	}
	Sample.FinalHeight = FMath::Clamp(Sample.WeightedHeight, -200.0f, 200.0f);
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
