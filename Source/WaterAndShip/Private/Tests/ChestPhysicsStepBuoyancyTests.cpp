#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Buoyancy/SWPhysicsStepBuoyancy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChestPhysicsStepBuoyancyHitchTest,
	"ArtisticSW.Chest.PhysicsStepBuoyancy.HitchFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestPhysicsStepBuoyancyHitchTest::RunTest(const FString& Parameters)
{
	// Replay the recorded failure condition: one GT water sample spans 15 PT steps.
	FSWPhysicsBuoyancyPontoon Pontoon;
	Pontoon.WaterHeight = -47.4f;
	Pontoon.Radius = 50.0f;
	FSWBuoyancyForceSettings Settings;
	Settings.DeepWaterBuoyancyMultiplier = 3.0f;
	constexpr float Mass = 25.0f;
	constexpr float Dt = 1.0f / 60.0f;
	FVector Position(0.0, 0.0, -111.316);
	FVector Velocity(0.0, 0.0, 28.286);
	const float HeldForce = Pontoon.Solve(Position, Velocity, Settings).BuoyantForceZ;
	float HeldVelocity = Velocity.Z;
	float PeakVelocity = Velocity.Z;
	for (int32 Step = 0; Step < 15; ++Step)
	{
		HeldVelocity += (HeldForce / Mass - 980.0f) * Dt;
		const auto Result = Pontoon.Solve(Position, Velocity, Settings);
		Velocity.Z += (Result.BuoyantForceZ / Mass - 980.0f) * Dt;
		Position.Z += Velocity.Z * Dt;
		PeakVelocity = FMath::Max(PeakVelocity, static_cast<float>(Velocity.Z));
	}
	TestTrue(TEXT("Held GT force reproduces launch above 8 m/s"), HeldVelocity > 800.0f);
	TestTrue(TEXT("Per-step feedback remains below the launch threshold"), PeakVelocity < 400.0f);
	TestTrue(TEXT("Upward motion reduces buoyancy before GT supplies another water sample"),
		Pontoon.Solve(Position, Velocity, Settings).BuoyantForceZ < HeldForce);

	TestEqual(TEXT("A pontoon above the sampled water gets no stale upward force"),
		Pontoon.Solve(FVector(0.0, 0.0, 10.0), FVector::ZeroVector, Settings).BuoyantForceZ, 0.0f);
	TestEqual(TEXT("Current upward velocity suppresses deep-water lift immediately"),
		Pontoon.Solve(FVector(0.0, 0.0, -111.316), FVector(0.0, 0.0, 829.049), Settings).BuoyantForceZ, 0.0f);
	return true;
}

#endif
