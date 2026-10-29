#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Misc/AutomationTest.h"
#include "Development/SWDevelopmentOptions.h"
#include "Engine/World.h"
#include "Room/SWRoomReadyState.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWDevelopmentShipSpeedTest,
	"ArtisticSW.Development.ShipSpeed.WorldScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWDevelopmentShipSpeedTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UWorld> FirstWorld(UWorld::CreateWorld(EWorldType::Game, false));
	TStrongObjectPtr<UWorld> SecondWorld(UWorld::CreateWorld(EWorldType::Game, false));
	ASWRoomReadyState* FirstState = FirstWorld->SpawnActor<ASWRoomReadyState>();
	ASWRoomReadyState* SecondState = SecondWorld->SpawnActor<ASWRoomReadyState>();
	if (TestNotNull(TEXT("First state"), FirstState) && TestNotNull(TEXT("Second state"), SecondState))
	{
		TestEqual(TEXT("Default multiplier"), SWDevelopmentOptions::GetShipSpeedMultiplier(FirstState), 1.0f);
		TestTrue(TEXT("Authority sets shared world value without a test session"), SWDevelopmentOptions::SetShipSpeedMultiplier(FirstWorld.Get(), 3.0f));
		TestEqual(TEXT("World reader sees authoritative value"), SWDevelopmentOptions::GetShipSpeedMultiplier(FirstState), 3.0f);
		TestEqual(TEXT("Other PIE world remains independent"), SWDevelopmentOptions::GetShipSpeedMultiplier(SecondState), 1.0f);
		TestFalse(TEXT("Non-finite request rejected"), SWDevelopmentOptions::SetShipSpeedMultiplier(FirstState, std::numeric_limits<float>::infinity()));
		TestEqual(TEXT("Invalid request preserves value"), SWDevelopmentOptions::GetShipSpeedMultiplier(FirstState), 3.0f);
		TestTrue(TEXT("Negative request accepted as zero"), SWDevelopmentOptions::SetShipSpeedMultiplier(FirstState, -2.0f));
		TestEqual(TEXT("Zero propulsion"), SWDevelopmentOptions::GetShipSpeedMultiplier(FirstState), 0.0f);
		TestTrue(TEXT("Reset accepted"), SWDevelopmentOptions::SetShipSpeedMultiplier(FirstState, 1.0f));
		TestEqual(TEXT("Original propulsion restored"), SWDevelopmentOptions::GetShipSpeedMultiplier(FirstState), 1.0f);
	}
	FirstWorld->DestroyWorld(false);
	SecondWorld->DestroyWorld(false);
	return !HasAnyErrors();
}
#endif
