#include "Misc/AutomationTest.h"
#include "SWShipWakeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SWShipWakeTest
{
	FSWShipWakeEvent MakeEvent()
	{
		FSWShipWakeEvent Event;
		Event.EventId = 1;
		Event.Origin = FVector2D::ZeroVector; // Stern.
		Event.Forward = FVector2D(1.0, 0.0);
		Event.UpdateServerTime = 10.0;
		Event.Amplitude = 65.0f;
		Event.SpeedCmPerSecond = 1200.0f;
		Event.AdvectionSpeedCmPerSecond = 1200.0f;
		Event.HullLengthCm = 2400.0f;
		Event.BeamWidthCm = 600.0f;
		Event.DraftCm = 250.0f;
		Event.WakeLengthCm = 16000.0f;
		Event.StateLifetime = 1.0f;
		Event.TransverseStrength = 0.55f;
		Event.DivergentStrength = 1.0f;
		Event.SternStrength = 0.72f;
		Event.SternPhaseOffsetRadians = 2.15f;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWShipWakeEvaluatorShapeTest,
	"ArtisticSW.Water.ShipWake.M2EvaluatorShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeEvaluatorShapeTest::RunTest(const FString& Parameters)
{
	const FSWShipWakeEvent Event = SWShipWakeTest::MakeEvent();
	const TArray<FSWShipWakeEvent> Events { Event };
	const double SampleTime = 10.1;

	float MaximumInsideHeight = 0.0f;
	for (float Downstream = 1000.0f; Downstream <= 10000.0f; Downstream += 500.0f)
	{
		for (float Lateral = -Downstream * 0.30f; Lateral <= Downstream * 0.30f; Lateral += 200.0f)
		{
			const FVector2D Position(Event.HullLengthCm - Downstream, Lateral);
			MaximumInsideHeight = FMath::Max(
				MaximumInsideHeight,
				FMath::Abs(FSWShipWakeEvaluator::EvaluateHeight(Position, SampleTime, Events)));
		}
	}
	TestTrue(TEXT("Directional spectrum produces measurable signed displacement inside the Kelvin wedge"),
		MaximumInsideHeight > 1.0f);

	const float AheadOfBowHeight = FSWShipWakeEvaluator::EvaluateHeight(
		FVector2D(Event.HullLengthCm + 500.0f, 0.0f), SampleTime, Events);
	TestTrue(TEXT("The steady wake does not displace water ahead of the bow"),
		FMath::IsNearlyZero(AheadOfBowHeight));

	const float OutsideWedgeHeight = FSWShipWakeEvaluator::EvaluateHeight(
		FVector2D(-6000.0f, 5000.0f), SampleTime, Events);
	TestTrue(TEXT("Finite spectrum leakage is suppressed outside Kelvin's cusp envelope"),
		FMath::IsNearlyZero(OutsideWedgeHeight));

	FSWShipWakeEvent BowOnly = Event;
	BowOnly.SternStrength = 0.0f;
	const TArray<FSWShipWakeEvent> BowOnlyEvents { BowOnly };
	const FVector2D InterferenceSample(-3500.0f, 650.0f);
	const float TwoSourceHeight = FSWShipWakeEvaluator::EvaluateHeight(InterferenceSample, SampleTime, Events);
	const float BowOnlyHeight = FSWShipWakeEvaluator::EvaluateHeight(InterferenceSample, SampleTime, BowOnlyEvents);
	TestTrue(TEXT("The separated stern source changes the bow spectrum through phase interference"),
		!FMath::IsNearlyEqual(TwoSourceHeight, BowOnlyHeight, 0.05f));

	const float ExpiredHeight = FSWShipWakeEvaluator::EvaluateHeight(
		InterferenceSample, 11.1, Events);
	TestTrue(TEXT("A stale hull state expires"), FMath::IsNearlyZero(ExpiredHeight));

	const FVector2D Gradient = FSWShipWakeEvaluator::EvaluateGradient(
		InterferenceSample, SampleTime, Events);
	TestTrue(TEXT("M2 gradient remains finite"),
		FMath::IsFinite(Gradient.X) && FMath::IsFinite(Gradient.Y));
	return true;
}

#endif
