#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Ship.h"
#include "ShipPhysicsAsync.h"
#include "Water/SWBuoyancyMath.h"

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

	FNetInputShip BeforeDeath;
	BeforeDeath.bBuoyancyEnabled = true;
	FNetInputShip AfterDeath;
	AfterDeath.bBuoyancyEnabled = false;
	FNetInputShip Interpolated;
	Interpolated.InterpolateData(BeforeDeath, AfterDeath, 1.0f);
	TestFalse(TEXT("Discrete interpolation selects the later death state"), Interpolated.bBuoyancyEnabled);
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

	Async.ValidateInput_Internal(Input);

	TestEqual(TEXT("MovementInput is sanitized to 0.0f when anchor is dropped"), Input.MovementInput, 0.0f);
	TestEqual(TEXT("SteeringInput is sanitized to 0.0f when anchor is dropped"), Input.SteeringInput, 0.0f);
	TestTrue(TEXT("Anchor dropped state is preserved"), Input.bIsAnchorDropped);
	return true;
}

#endif
