#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Ship.h"

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

#endif
