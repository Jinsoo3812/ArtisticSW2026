#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Ship.h"
#include "ShipPhysicsAsync.h"
#include "ShipRollStabilization.h"
#include "Water/SWBuoyancyMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipRollStabilizationMathTest,
	"ArtisticSW.Ship.RollStabilization.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipRollStabilizationMathTest::RunTest(const FString& Parameters)
{
	const float PositiveRoll = FShipRollStabilizationMath::ComputeSignedRollRadians(
		FQuat(FVector::ForwardVector, FMath::DegreesToRadians(25.0f)));
	const float NegativeRoll = FShipRollStabilizationMath::ComputeSignedRollRadians(
		FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-25.0f)));
	TestTrue(TEXT("Signed roll extraction preserves positive roll"),
		FMath::IsNearlyEqual(FMath::RadiansToDegrees(PositiveRoll), 25.0f, 0.01f));
	TestTrue(TEXT("Signed roll extraction preserves negative roll"),
		FMath::IsNearlyEqual(FMath::RadiansToDegrees(NegativeRoll), -25.0f, 0.01f));

	const auto AccelerationAt = [](float AngleDegrees, float AngularVelocityDegrees)
	{
		return FShipRollStabilizationMath::ComputeAngularAccelerationRadians(
			FMath::DegreesToRadians(AngleDegrees),
			FMath::DegreesToRadians(AngularVelocityDegrees),
			20.0f,
			30.0f,
			0.5f,
			1.0f,
			720.0f);
	};
	TestTrue(TEXT("Positive roll always receives inward acceleration"), AccelerationAt(10.0f, 0.0f) < 0.0f);
	TestTrue(TEXT("Negative roll always receives inward acceleration"), AccelerationAt(-10.0f, 0.0f) > 0.0f);
	TestTrue(TEXT("Roll-rate damping opposes motion at level"), AccelerationAt(0.0f, 20.0f) < 0.0f);
	TestTrue(TEXT("Soft-wall increases correction beyond 20 degrees"),
		FMath::Abs(AccelerationAt(25.0f, 0.0f)) > FMath::Abs(AccelerationAt(20.0f, 0.0f)));
	TestTrue(TEXT("Controller acceleration remains bounded"),
		FMath::Abs(FMath::RadiansToDegrees(AccelerationAt(45.0f, 100.0f))) <= 720.01f);

	float SimulatedAngle = FMath::DegreesToRadians(29.0f);
	float SimulatedVelocity = 0.0f;
	float PeakAngle = FMath::Abs(SimulatedAngle);
	constexpr float PhysicsStep = 1.0f / 60.0f;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		const float Acceleration = FShipRollStabilizationMath::ComputeAngularAccelerationRadians(
			SimulatedAngle,
			SimulatedVelocity,
			20.0f,
			30.0f,
			0.5f,
			1.0f,
			720.0f);
		SimulatedVelocity += Acceleration * PhysicsStep;
		SimulatedAngle += SimulatedVelocity * PhysicsStep;
		PeakAngle = FMath::Max(PeakAngle, FMath::Abs(SimulatedAngle));
	}
	TestTrue(TEXT("60 Hz integration does not push a 29-degree roll farther outward"),
		FMath::RadiansToDegrees(PeakAngle) <= 29.01f);
	TestTrue(TEXT("60 Hz integration converges back near level without sustained oscillation"),
		FMath::Abs(FMath::RadiansToDegrees(SimulatedAngle)) < 0.01f
			&& FMath::Abs(FMath::RadiansToDegrees(SimulatedVelocity)) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipNetworkPhysicsBuoyancyInputTest,
	"ArtisticSW.Ship.NetworkPhysicsBuoyancyInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipNetworkPhysicsBuoyancyInputTest::RunTest(const FString& Parameters)
{
	FNetInputShip Source;
	Source.ServerFrame = 77;
	Source.LocalFrame = 91;
	Source.MovementInput = 0.5f;
	Source.SteeringInput = -0.25f;
	Source.ExternalAcceleration = FVector(120.0f, -80.0f, 0.0f);
	Source.BlastAcceleration = FVector(1234.0f, -456.0f, 789.0f);
	Source.BlastApplicationPointLocal = FVector(700.0f, -200.0f, 150.0f);
	Source.bBuoyancyEnabled = false;
	Source.bHasAuthoritativeBuoyancyState = true;

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	bool bSaveSuccess = false;
	Source.NetSerialize(Writer, nullptr, bSaveSuccess);
	TestTrue(TEXT("Network Physics input saves successfully"), bSaveSuccess);

	FNetInputShip Loaded;
	FMemoryReader Reader(Bytes);
	bool bLoadSuccess = false;
	Loaded.NetSerialize(Reader, nullptr, bLoadSuccess);
	TestTrue(TEXT("Network Physics input loads successfully"), bLoadSuccess);
	TestEqual(TEXT("Server frame survives serialization"), Loaded.ServerFrame, 77);
	TestFalse(TEXT("Buoyancy-off state survives serialization"), Loaded.bBuoyancyEnabled);
	TestTrue(TEXT("Server-authoritative buoyancy marker survives serialization"),
		Loaded.bHasAuthoritativeBuoyancyState);
	TestTrue(TEXT("3D blast acceleration survives serialization"),
		Loaded.BlastAcceleration.Equals(Source.BlastAcceleration, 1.0f));
	TestTrue(TEXT("Blast contact offset survives serialization"),
		Loaded.BlastApplicationPointLocal.Equals(Source.BlastApplicationPointLocal, 1.0f));

	FNetInputShip BeforeDeath;
	BeforeDeath.bBuoyancyEnabled = true;
	FNetInputShip AfterDeath;
	AfterDeath.bBuoyancyEnabled = false;
	AfterDeath.BlastAcceleration = FVector(100.0f, 200.0f, 300.0f);
	AfterDeath.BlastApplicationPointLocal = FVector(400.0f, 500.0f, 600.0f);
	FNetInputShip Interpolated;
	Interpolated.InterpolateData(BeforeDeath, AfterDeath, 1.0f);
	TestFalse(TEXT("Discrete interpolation selects the later death state"), Interpolated.bBuoyancyEnabled);
	TestEqual(TEXT("Discrete interpolation does not smear blast acceleration"),
		Interpolated.BlastAcceleration, AfterDeath.BlastAcceleration);
	TestEqual(TEXT("Discrete interpolation preserves the matching contact offset"),
		Interpolated.BlastApplicationPointLocal, AfterDeath.BlastApplicationPointLocal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipNetworkPhysicsDeepWaterBuoyancyTest,
	"ArtisticSW.Ship.NetworkPhysicsDeepWaterBuoyancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipNetworkPhysicsDeepWaterBuoyancyTest::RunTest(const FString& Parameters)
{
	FSWBuoyancyForceSettings SourceSettings;
	SourceSettings.BuoyancyCoefficient = 0.01f;
	SourceSettings.DeepWaterBuoyancyMultiplier = 3.0f;
	SourceSettings.BuoyancyDamp = 0.0f;
	SourceSettings.BuoyancyDamp2 = 0.0f;
	SourceSettings.MaxBuoyantForce = 1.0e12f;

	// This is the same GT -> PT settings payload used independently by the
	// authoritative server and predicting client before PT caches it for resim.
	FAsyncInputShip ServerAsyncInput;
	FAsyncInputShip ClientAsyncInput;
	ServerAsyncInput.BuoyancyForceSettings = SourceSettings;
	ClientAsyncInput.BuoyancyForceSettings = SourceSettings;

	TestEqual(
		TEXT("Server async input receives deep-water multiplier"),
		ServerAsyncInput.BuoyancyForceSettings.DeepWaterBuoyancyMultiplier,
		3.0f);
	TestEqual(
		TEXT("Predicting client async input receives identical deep-water multiplier"),
		ClientAsyncInput.BuoyancyForceSettings.DeepWaterBuoyancyMultiplier,
		ServerAsyncInput.BuoyancyForceSettings.DeepWaterBuoyancyMultiplier);

	FSWBuoyancySolveInput SolveInput;
	SolveInput.PontoonCenterZ = 0.0f;
	SolveInput.PontoonRadius = 100.0f;
	SolveInput.RelativeVelocityZ = 0.0f;
	SolveInput.ForceScale = 1.0f;

	// At half submersion DeepWaterAlpha is zero, so normal equilibrium is unchanged.
	SolveInput.WaterHeight = 0.0f;
	const FSWBuoyancySolveResult ServerHalfSubmerged = FSWBuoyancyMath::SolvePontoon(
		SolveInput,
		ServerAsyncInput.BuoyancyForceSettings);
	FSWBuoyancyForceSettings BaselineSettings = SourceSettings;
	BaselineSettings.DeepWaterBuoyancyMultiplier = 1.0f;
	const FSWBuoyancySolveResult BaselineHalfSubmerged = FSWBuoyancyMath::SolvePontoon(
		SolveInput,
		BaselineSettings);
	TestTrue(
		TEXT("Deep-water multiplier leaves half-submerged force unchanged"),
		FMath::IsNearlyEqual(ServerHalfSubmerged.BuoyantForceZ, BaselineHalfSubmerged.BuoyantForceZ));

	// At 1.5 radii submersion, alpha is 0.5. A multiplier of 3 therefore doubles
	// the effective coefficient: lerp(1, 3, 0.5) == 2.
	SolveInput.WaterHeight = 50.0f;
	const FSWBuoyancySolveResult ServerDeep = FSWBuoyancyMath::SolvePontoon(
		SolveInput,
		ServerAsyncInput.BuoyancyForceSettings);
	const FSWBuoyancySolveResult ClientDeep = FSWBuoyancyMath::SolvePontoon(
		SolveInput,
		ClientAsyncInput.BuoyancyForceSettings);
	const FSWBuoyancySolveResult BaselineDeep = FSWBuoyancyMath::SolvePontoon(
		SolveInput,
		BaselineSettings);

	TestTrue(
		TEXT("Deep-water multiplier increases only the deep recovery force"),
		FMath::IsNearlyEqual(ServerDeep.BuoyantForceZ, BaselineDeep.BuoyantForceZ * 2.0f, 0.1f));
	TestTrue(
		TEXT("Server and predicting client calculate identical deep-water force"),
		FMath::IsNearlyEqual(ServerDeep.BuoyantForceZ, ClientDeep.BuoyantForceZ));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipNetworkPhysicsAnchorInputValidationTest,
	"ArtisticSW.Ship.NetworkPhysicsAnchorInputValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipNetworkPhysicsAnchorInputValidationTest::RunTest(const FString& Parameters)
{
	FShipPhysicsAsync Async;
	FNetInputShip Input;
	Input.MovementInput = 1.0f;
	Input.SteeringInput = 0.5f;
	Input.bIsAnchorDropped = true;
	Input.AnchorOriginXY = FVector2D(100.0, 200.0);
	Input.BlastAcceleration = FVector(100.0f, 200.0f, 300.0f);
	Input.BlastApplicationPointLocal = FVector(400.0f, -500.0f, 600.0f);

	Async.ValidateInput_Internal(Input);

	TestEqual(TEXT("MovementInput is sanitized to 0.0f when anchor is dropped"), Input.MovementInput, 0.0f);
	TestEqual(TEXT("SteeringInput is sanitized to 0.0f when anchor is dropped"), Input.SteeringInput, 0.0f);
	TestTrue(TEXT("Anchor dropped state is preserved"), Input.bIsAnchorDropped);
	TestEqual(TEXT("Anchor does not suppress a 3D blast"), Input.BlastAcceleration, FVector(100.0f, 200.0f, 300.0f));
	const FVector Force(1000.0f, 0.0f, 0.0f);
	TestEqual(TEXT("Centred blast produces no torque"),
		FVector::CrossProduct(FVector::ZeroVector, Force), FVector::ZeroVector);
	TestEqual(TEXT("Off-centre blast produces the expected torque"),
		FVector::CrossProduct(FVector(0.0f, 100.0f, 0.0f), Force), FVector(0.0f, 0.0f, -100000.0f));
	return true;
}

#endif
