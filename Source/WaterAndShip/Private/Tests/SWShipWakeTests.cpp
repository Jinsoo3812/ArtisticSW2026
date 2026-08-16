#include "Misc/AutomationTest.h"
#include "SWKelvinWakeAtlas.h"
#include "SWShipWakeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SWShipWakeTest
{
	FSWShipWakeEvent MakeEvent()
	{
		FSWShipWakeEvent Event;
		Event.EventId = 1;
		Event.Origin = FVector2D::ZeroVector;
		Event.Forward = FVector2D(1.0, 0.0);
		Event.TrajectoryPoints = { FVector2D::ZeroVector, FVector2D(-500.0, 0.0) };
		Event.UpdateServerTime = 10.0;
		Event.Amplitude = 65.0f;
		Event.SpeedCmPerSecond = 1200.0f;
		Event.AdvectionSpeedCmPerSecond = 0.0f;
		Event.PressureSizeCm = 2400.0f;
		Event.LongitudinalScale = 1.0f;
		Event.LateralScale = 1.0f;
		Event.StateLifetime = 1.0f;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWShipWakeEvaluatorShapeTest,
	"ArtisticSW.Water.ShipWake.M4AtlasShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeEvaluatorShapeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("The baked FP16 atlas loads"), FSWKelvinWakeAtlas::Get().Initialize());
	const FSWShipWakeEvent Event = SWShipWakeTest::MakeEvent();
	const TArray<FSWShipWakeEvent> Events { Event };
	const double SampleTime = 10.1;

	float MaximumInsideHeight = 0.0f;
	for (float Downstream = 500.0f; Downstream <= 50000.0f; Downstream += 500.0f)
	{
		for (float Lateral = -Downstream * 0.30f; Lateral <= Downstream * 0.30f; Lateral += 250.0f)
		{
			MaximumInsideHeight = FMath::Max(MaximumInsideHeight, FMath::Abs(
				FSWShipWakeEvaluator::EvaluateHeight(
					FVector2D(-Downstream, Lateral), SampleTime, Events)));
		}
	}
	TestTrue(TEXT("The golden atlas produces measurable signed displacement inside its V"),
		MaximumInsideHeight > 1.0f);

	TestTrue(TEXT("The wake does not displace water ahead of the apex"), FMath::IsNearlyZero(
		FSWShipWakeEvaluator::EvaluateHeight(FVector2D(500.0f, 0.0f), SampleTime, Events)));
	TestTrue(TEXT("The finite baked lateral domain rejects distant points"), FMath::IsNearlyZero(
		FSWShipWakeEvaluator::EvaluateHeight(FVector2D(-6000.0f, 50000.0f), SampleTime, Events)));
	TestTrue(TEXT("A stale vessel state expires"), FMath::IsNearlyZero(
		FSWShipWakeEvaluator::EvaluateHeight(FVector2D(-5000.0f, 0.0f), 11.1, Events)));

	const FVector2D Gradient = FSWShipWakeEvaluator::EvaluateGradient(
		FVector2D(-7000.0f, 1000.0f), SampleTime, Events);
	TestTrue(TEXT("M4 atlas gradient remains finite"),
		FMath::IsFinite(Gradient.X) && FMath::IsFinite(Gradient.Y));
	return true;
}

#endif
