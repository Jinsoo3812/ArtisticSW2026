#include "SWCharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <limits>

namespace
{
	bool RoundTripSurfaceWaveMoveData(
		FAutomationTestBase& Test,
		bool bHasTime,
		double Time,
		bool bExpectSuccess)
	{
		UCharacterMovementComponent* Movement = NewObject<UCharacterMovementComponent>();
		FCharacterNetworkMoveData_SWCharacter Source;
		Source.bHasSurfaceWaveServerTime = bHasTime;
		Source.SurfaceWaveServerTimeSeconds = Time;

		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		const bool bSaved = Source.Serialize(
			*Movement, Writer, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
		if (!bSaved)
		{
			return Test.TestFalse(TEXT("Unexpected save failure"), bExpectSuccess);
		}

		FCharacterNetworkMoveData_SWCharacter Loaded;
		FMemoryReader Reader(Bytes);
		const bool bLoaded = Loaded.Serialize(
			*Movement, Reader, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
		Test.TestEqual(TEXT("Load result"), bLoaded, bExpectSuccess);
		if (bExpectSuccess)
		{
			Test.TestEqual(TEXT("Validity round trip"), Loaded.bHasSurfaceWaveServerTime, bHasTime);
			Test.TestEqual(TEXT("Time round trip"), Loaded.SurfaceWaveServerTimeSeconds, bHasTime ? Time : 0.0);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSurfaceWaveMoveDataSerializationTest,
	"ArtisticSW.Swimming.Network.SurfaceWaveMoveDataSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSurfaceWaveMoveDataSerializationTest::RunTest(const FString& Parameters)
{
	RoundTripSurfaceWaveMoveData(*this, false, 123.0, true);
	RoundTripSurfaceWaveMoveData(*this, true, 0.0, true);
	RoundTripSurfaceWaveMoveData(*this, true, 42.125, true);
	RoundTripSurfaceWaveMoveData(*this, true, 1000000000.25, true);
	RoundTripSurfaceWaveMoveData(*this, true, -1.0, false);
	RoundTripSurfaceWaveMoveData(*this, true, std::numeric_limits<double>::quiet_NaN(), false);
	RoundTripSurfaceWaveMoveData(*this, true, std::numeric_limits<double>::infinity(), false);
	return true;
}

#endif
