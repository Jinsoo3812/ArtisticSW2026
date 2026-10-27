#include "SWShipWakeRenderingDiagnostics.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWKelvinDiagnostics, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarDiagnosticEnable(TEXT("sw.ShipWake.Diag.Enable"), 0,
		TEXT("Enable Kelvin emission, boundary and prediction diagnostics (CPU render-input estimates)."));
	// 예측과 권위를 별도 파동으로 합산하지 않도록 렌더 기본값만 권위 전용으로 둔다.
	TAutoConsoleVariable<int32> CVarRenderEvents(TEXT("sw.ShipWake.Diag.RenderEvents"), 1,
		TEXT("Rendering only: 0=all, 1=authoritative (default), 2=predicted. Does not change water queries or physics."));
	TAutoConsoleVariable<float> CVarLogInterval(TEXT("sw.ShipWake.Diag.Interval"), 0.5f,
		TEXT("Kelvin diagnostic summary interval in seconds (minimum 0.1)."));
	TAutoConsoleVariable<float> CVarProbeSpacing(TEXT("sw.ShipWake.Diag.ProbeSpacing"), 1000.0f,
		TEXT("Spacing in cm of 3x3 fixed-world probes captured at the camera grid center."));
	TAutoConsoleVariable<int32> CVarResetProbe(TEXT("sw.ShipWake.Diag.ResetProbe"), 0,
		TEXT("Change this value to recapture the fixed-world probe center at the current camera."));

	FString DescribeProbe(const FVector2D& Point, const double Time, const float Height,
		const float Delta, TConstArrayView<FSWShipWakeEvent> Events)
	{
		FString Details;
		int32 Printed = 0;
		int32 Contributing = 0;
		int32 EndpointContributors = 0;
		float Predicted = 0.0f;
		float Authoritative = 0.0f;
		for (const FSWShipWakeEvent& Event : Events)
		{
			const FSWShipWakeEventDiagnosticSample Sample =
				FSWShipWakeEvaluator::EvaluateEventDiagnostic(Point, Time, Event);
			if (Event.EventId < 0) Predicted += Sample.Height;
			else Authoritative += Sample.Height;
			if (FMath::Abs(Sample.Height) <= 0.001f) continue;
			++Contributing;
			if (Sample.Alpha <= 0.01f || Sample.Alpha >= 0.99f) ++EndpointContributors;
			if (Printed++ < 6)
			{
				Details += FString::Printf(TEXT(" E%d:%s H=%.3f Alpha=%.5f Age=%.3f Mask=%.4f Fade=%.3f Amp=%.3f A0=%.3f A1=%.3f EmitT=%.6f"),
					Event.EventId, Event.EventId < 0 ? TEXT("P") : TEXT("A"), Sample.Height,
					Sample.Alpha, Sample.Age, Sample.StampMask, Sample.FadeIn, Sample.AmplitudeCm,
					Event.InitialAmplitudeCm, Event.GetEndAmplitudeCm(),
					Time - Sample.Age);
			}
		}
		return FString::Printf(TEXT("T=%.6f Q=(%.1f,%.1f) H=%.3f Delta=%.3f RawA=%.3f RawP=%.3f Contributors=%d EndpointContributors=%d%s"),
			Time, Point.X, Point.Y, Height, Delta, Authoritative, Predicted,
			Contributing, EndpointContributors, *Details);
	}
}

bool FSWShipWakeRenderingDiagnostics::IsEnabled()
{
	return CVarDiagnosticEnable.GetValueOnAnyThread() != 0;
}

int32 FSWShipWakeRenderingDiagnostics::GetRenderEventMode()
{
	return FMath::Clamp(CVarRenderEvents.GetValueOnGameThread(), 0, 2);
}

void FSWShipWakeRenderingDiagnostics::FilterRenderingEvents(TArray<FSWShipWakeEvent>& Events)
{
	const int32 Mode = GetRenderEventMode();
	if (Mode == 0) return;
	Events.RemoveAll([Mode](const FSWShipWakeEvent& Event)
	{
		return Mode == 1 ? Event.EventId < 0 : Event.EventId >= 0;
	});
}

void FSWShipWakeRenderingDiagnostics::Update(const UWorld* World, const double ServerTime,
	const FVector2D& GridCenter, TConstArrayView<FSWShipWakeEvent> RenderingEvents,
	const ESWKelvinFroudeProfile Profile)
{
	if (!IsEnabled())
	{
		PreviousEvents.Reset();
		PreviousTime = -1.0;
		return;
	}
	const int32 Mode = GetRenderEventMode();
	const int32 Reset = CVarResetProbe.GetValueOnGameThread();
	const float Spacing = FMath::Max(CVarProbeSpacing.GetValueOnGameThread(), 1.0f);
	const FString Context = FString::Printf(TEXT("World=%s Net=%d Mode=%d Profile=%d"),
		*GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : -1, Mode, static_cast<int32>(Profile));
	if (PreviousTime < 0.0 || ServerTime < PreviousTime || Reset != LastReset
		|| Mode != LastMode || Profile != LastProfile || Spacing != LastSpacing)
	{
		ProbeCenter = GridCenter;
		PreviousEvents.Reset();
		PreviousTime = -1.0;
		LastLogTime = ServerTime;
		MaxSetDelta = MaxFrameDelta = 0.0f;
		SetEvidence.Reset();
		FrameEvidence.Reset();
		PreviousFrameEvidence.Reset();
		LastMode = Mode;
		LastReset = Reset;
		LastSpacing = Spacing;
		LastProfile = Profile;
		UE_LOG(LogSWKelvinDiagnostics, Log,
			TEXT("[KelvinDiag:Probe] %s T=%.6f Center=(%.1f,%.1f) Spacing=%.1f CPU_ESTIMATE_NOT_GPU_READBACK"),
			*Context, ServerTime, ProbeCenter.X, ProbeCenter.Y, Spacing);
	}
	int32 ProbeIndex = 0;
	for (int32 Y = -1; Y <= 1; ++Y)
	{
		for (int32 X = -1; X <= 1; ++X, ++ProbeIndex)
		{
			const FVector2D Point = ProbeCenter + FVector2D(X * Spacing, Y * Spacing);
			const float Height = FSWShipWakeEvaluator::EvaluateHeight(Point, ServerTime, RenderingEvents);
			if (PreviousTime >= 0.0)
			{
				const float OldSetHeightAtNow = FSWShipWakeEvaluator::EvaluateHeight(Point, ServerTime, PreviousEvents);
				const float SetDelta = Height - OldSetHeightAtNow;
				const float FrameDelta = Height - PreviousHeights[ProbeIndex];
				if (FMath::Abs(SetDelta) > MaxSetDelta)
				{
					MaxSetDelta = FMath::Abs(SetDelta);
					SetEvidence = DescribeProbe(Point, ServerTime, Height, SetDelta, RenderingEvents)
						+ TEXT(" OldSetAtNow{") + DescribeProbe(Point, ServerTime, OldSetHeightAtNow, 0.0f, PreviousEvents) + TEXT("}");
				}
				if (FMath::Abs(FrameDelta) > MaxFrameDelta)
				{
					MaxFrameDelta = FMath::Abs(FrameDelta);
					FrameEvidence = FString::Printf(TEXT("Dt=%.6f "), ServerTime - PreviousTime)
						+ DescribeProbe(Point, ServerTime, Height, FrameDelta, RenderingEvents);
					PreviousFrameEvidence = FString::Printf(TEXT("ForT=%.6f "), ServerTime)
						+ DescribeProbe(Point, PreviousTime, PreviousHeights[ProbeIndex], 0.0f, PreviousEvents);
				}
			}
			PreviousHeights[ProbeIndex] = Height;
		}
	}
	const float Interval = FMath::Max(CVarLogInterval.GetValueOnGameThread(), 0.1f);
	if (ServerTime - LastLogTime >= Interval)
	{
		int32 PredictedCount = 0;
		int32 AuthoritativeCount = 0;
		int32 DuplicateCandidates = 0;
		int32 AdjacentCandidates = 0;
		float MaxAmplitudeGap = 0.0f;
		float MaxDirectionGap = 0.0f;
		FString BoundaryEvidence;
		for (int32 I = 0; I < RenderingEvents.Num(); ++I)
		{
			const FSWShipWakeEvent& A = RenderingEvents[I];
			if (!A.IsActiveAt(ServerTime)) continue;
			if (A.EventId < 0) ++PredictedCount;
			else ++AuthoritativeCount;
			for (int32 J = I + 1; J < RenderingEvents.Num(); ++J)
			{
				const FSWShipWakeEvent& B = RenderingEvents[J];
				if (!B.IsActiveAt(ServerTime)) continue;
				if ((A.EventId < 0) != (B.EventId < 0))
				{
					const double StartGap = FMath::Abs(A.StartServerTime - B.StartServerTime);
					const double EndGap = FMath::Abs(A.EndServerTime - B.EndServerTime);
					if (StartGap > 0.25 || EndGap > 0.25) continue;
					const double StartDistance = FVector2D::Distance(A.Origin, B.Origin);
					const double EndDistance = FVector2D::Distance(A.EndOrigin, B.EndOrigin);
					if (StartDistance > 750.0 || EndDistance > 750.0
						|| FVector2D::DotProduct(A.Forward.GetSafeNormal(), B.Forward.GetSafeNormal()) < 0.9
						|| FVector2D::DotProduct(A.EndForward.GetSafeNormal(), B.EndForward.GetSafeNormal()) < 0.9) continue;
					if (++DuplicateCandidates <= 4)
					{
						UE_LOG(LogSWKelvinDiagnostics, Log,
							TEXT("[KelvinDiag:3:DuplicateCandidate] %s T=%.6f E%d/E%d StartDt=%.4f EndDt=%.4f StartDist=%.1f EndDist=%.1f Amp=%.2f/%.2f HEURISTIC_NO_SOURCE_ID"),
							*Context, ServerTime, A.EventId, B.EventId, StartGap, EndGap, StartDistance,
							EndDistance, A.InitialAmplitudeCm, B.InitialAmplitudeCm);
					}
				}
				else if (FMath::Abs(A.EndServerTime - B.StartServerTime) <= 0.001
					&& FVector2D::Distance(A.EndOrigin, B.Origin) <= 1.0)
				{
					++AdjacentCandidates;
					const float AmplitudeGap = FMath::Abs(A.GetEndAmplitudeCm() - B.InitialAmplitudeCm);
					const float DirectionGap = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						static_cast<float>(FVector2D::DotProduct(A.EndForward.GetSafeNormal(), B.Forward.GetSafeNormal())), -1.0f, 1.0f)));
					if (BoundaryEvidence.IsEmpty() || AmplitudeGap > MaxAmplitudeGap || DirectionGap > MaxDirectionGap)
					{
						BoundaryEvidence = FString::Printf(TEXT("E%d->E%d JoinT=%.6f AmpGap=%.3f DirectionGapDeg=%.3f"),
							A.EventId, B.EventId, A.EndServerTime, AmplitudeGap, DirectionGap);
					}
					MaxAmplitudeGap = FMath::Max(MaxAmplitudeGap, AmplitudeGap);
					MaxDirectionGap = FMath::Max(MaxDirectionGap, DirectionGap);
				}
			}
		}
		UE_LOG(LogSWKelvinDiagnostics, Log,
			TEXT("[KelvinDiag:1:SetChange] %s T=%.6f ActiveA=%d ActiveP=%d MaxAbsSetDeltaCm=%.4f %s"),
			*Context, ServerTime, AuthoritativeCount, PredictedCount, MaxSetDelta, *SetEvidence);
		UE_LOG(LogSWKelvinDiagnostics, Log,
			TEXT("[KelvinDiag:2:Boundary] %s T=%.6f AdjacentCandidates=%d MaxAmpGap=%.3f MaxDirectionGapDeg=%.3f %s MaxAbsFrameDeltaCm=%.4f %s"),
			*Context, ServerTime, AdjacentCandidates, MaxAmplitudeGap, MaxDirectionGap, *BoundaryEvidence, MaxFrameDelta, *FrameEvidence);
		UE_LOG(LogSWKelvinDiagnostics, Log,
			TEXT("[KelvinDiag:3:Summary] %s T=%.6f DuplicateCandidates=%d StoredRenderEvents=%d CPU_ESTIMATE_NOT_GPU_READBACK"),
			*Context, ServerTime, DuplicateCandidates, RenderingEvents.Num());
		if (!PreviousFrameEvidence.IsEmpty())
		{
			UE_LOG(LogSWKelvinDiagnostics, Log, TEXT("[KelvinDiag:2:Previous] %s %s"), *Context, *PreviousFrameEvidence);
		}
		LastLogTime = ServerTime;
		MaxSetDelta = MaxFrameDelta = 0.0f;
		SetEvidence.Reset();
		FrameEvidence.Reset();
		PreviousFrameEvidence.Reset();
	}
	PreviousEvents.Reset(RenderingEvents.Num());
	PreviousEvents.Append(RenderingEvents.GetData(), RenderingEvents.Num());
	PreviousTime = ServerTime;
}
