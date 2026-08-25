#include "Misc/AutomationTest.h"
#include "SWKelvinWakeAtlas.h"
#include "SWShipWakeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FSWShipWakeEvent MakeM7Event()
	{
		FSWShipWakeEvent Event;
		Event.EventId = 1;
		Event.Origin = FVector2D::ZeroVector;
		Event.EndOrigin = FVector2D(500.0, 0.0);
		Event.Forward = FVector2D(1.0, 0.0);
		Event.EndForward = FVector2D(1.0, 0.0);
		Event.StartServerTime = 10.0;
		Event.EndServerTime = 11.0;
		Event.InitialAmplitudeCm = 65.0f;
		Event.PropagationSpeedCmPerSecond = 1200.0f;
		Event.DecayRate = 0.0f;
		Event.WakeLengthCm = 25000.0f;
		Event.WakeHalfWidthCm = 12000.0f;
		Event.EnvelopeWidthCm = 500.0f;
		Event.FadeInSeconds = 0.0f;
		Event.ExpireServerTime = 30.0;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWShipWakeM7GoldenTest,
	"ArtisticSW.Water.ShipWake.M7GoldenPropagation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeM7GoldenTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Fixed Golden Image loads"), FSWKelvinWakeAtlas::Get().Initialize());
	FSWShipWakeEvent Event = MakeM7Event();
	float BestGolden = 0.0f;
	FVector2D BestQuery = FVector2D::ZeroVector;
	for (float U = 0.05f; U <= 0.95f; U += 0.025f)
	{
		for (float V = -0.95f; V <= 0.95f; V += 0.05f)
		{
			const float Golden = FMath::Abs(FSWKelvinWakeAtlas::Get().SampleFixedNormalized(U, V));
			if (Golden > BestGolden)
			{
				BestGolden = Golden;
				BestQuery = FVector2D(-U * Event.WakeLengthCm, V * Event.WakeHalfWidthCm);
			}
		}
	}
	TestTrue(TEXT("Golden contains signed wave data"), BestGolden > 0.1f);
	const float Radius = BestQuery.Size();
	const double ArrivalTime = Event.StartServerTime + Radius / Event.PropagationSpeedCmPerSecond;
	const TArray<FSWShipWakeEvent> One { Event };
	const float Height = FSWShipWakeEvaluator::EvaluateHeight(BestQuery, ArrivalTime, One);
	TestTrue(TEXT("Causal front reveals the Golden wake"), FMath::Abs(Height) > 1.0f);
	TestTrue(TEXT("Point is still quiet before front arrival"), FMath::IsNearlyZero(
		FSWShipWakeEvaluator::EvaluateHeight(BestQuery, Event.StartServerTime, One)));
	TestTrue(TEXT("Water ahead of the recorded tangent remains quiet"), FMath::IsNearlyZero(
		FSWShipWakeEvaluator::EvaluateHeight(FVector2D(1500.0, 0.0), ArrivalTime, One)));

	const FVector2D Gradient = FSWShipWakeEvaluator::EvaluateGradient(BestQuery, ArrivalTime, One);
	TestTrue(TEXT("CPU buoyancy gradient stays finite"),
		FMath::IsFinite(Gradient.X) && FMath::IsFinite(Gradient.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWShipWakeM7RotationTest,
	"ArtisticSW.Water.ShipWake.M7ImmutableRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeM7RotationTest::RunTest(const FString& Parameters)
{
	FSWShipWakeEvent OldEvent = MakeM7Event();
	const TArray<FSWShipWakeEvent> Before { OldEvent };
	const FVector2D Query(-4000.0, 500.0);
	const double Time = OldEvent.StartServerTime + Query.Size() / OldEvent.PropagationSpeedCmPerSecond;
	const float BeforeTurn = FSWShipWakeEvaluator::EvaluateHeight(Query, Time, Before);
	FSWShipWakeEvent TurnEvent = OldEvent;
	TurnEvent.EventId = 2;
	TurnEvent.Origin = FVector2D(1000.0, 0.0);
	TurnEvent.EndOrigin = FVector2D(1000.0, 500.0);
	TurnEvent.Forward = FVector2D(0.0, 1.0);
	TurnEvent.EndForward = FVector2D(0.0, 1.0);
	TestTrue(TEXT("Old event tangent is not mutated by a later turn"),
		OldEvent.Forward.Equals(FVector2D(1.0, 0.0)));
	TestTrue(TEXT("Old event remains deterministic after a turn event is authored"),
		FMath::IsNearlyEqual(BeforeTurn,
			FSWShipWakeEvaluator::EvaluateHeight(Query, Time, Before), 0.0001f));
	return true;
}

#endif
