#include "Misc/AutomationTest.h"
#include "SWShipWakeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWShipWakeEvaluatorShapeTest,
	"ArtisticSW.Water.ShipWake.EvaluatorShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeEvaluatorShapeTest::RunTest(const FString& Parameters)
{
	FSWShipWakeEvent Event;
	Event.EventId = 1;
	Event.Origin = FVector2D::ZeroVector;
	Event.Forward = FVector2D(1.0, 0.0);
	Event.StartServerTime = 10.0;
	Event.InitialAmplitude = 40.0f;
	Event.WaveLength = 600.0f;
	Event.PhaseSpeed = 600.0f;
	Event.Lifetime = 10.0f;
	Event.KelvinHalfAngleRadians = FMath::DegreesToRadians(19.47f);
	const TArray<FSWShipWakeEvent> Events { Event };

	const double SampleTime = 11.0;
	const float Radius = Event.PhaseSpeed * static_cast<float>(SampleTime - Event.StartServerTime);
	const FVector2D OnDivergentArm(
		-Radius * FMath::Cos(Event.KelvinHalfAngleRadians),
		Radius * FMath::Sin(Event.KelvinHalfAngleRadians));
	const float ArmHeight = FSWShipWakeEvaluator::EvaluateHeight(OnDivergentArm, SampleTime, Events);
	TestTrue(TEXT("The Kelvin divergent arm has a measurable signed height"), FMath::Abs(ArmHeight) > 1.0f);

	const float AheadHeight = FSWShipWakeEvaluator::EvaluateHeight(FVector2D(500.0, 0.0), SampleTime, Events);
	TestTrue(TEXT("Wake packets do not displace water ahead of the ship"), FMath::IsNearlyZero(AheadHeight));

	const FVector2D OutsideWedge(-300.0, 800.0);
	const float OutsideHeight = FSWShipWakeEvaluator::EvaluateHeight(OutsideWedge, SampleTime, Events);
	TestTrue(TEXT("Wake packets are culled outside the Kelvin wedge"), FMath::IsNearlyZero(OutsideHeight));

	const float ExpiredHeight = FSWShipWakeEvaluator::EvaluateHeight(OnDivergentArm, 21.0, Events);
	TestTrue(TEXT("Expired wake packets have no height"), FMath::IsNearlyZero(ExpiredHeight));

	const FVector2D Gradient = FSWShipWakeEvaluator::EvaluateGradient(OnDivergentArm, SampleTime, Events);
	TestTrue(TEXT("Gradient remains finite"), FMath::IsFinite(Gradient.X) && FMath::IsFinite(Gradient.Y));
	return true;
}

#endif

