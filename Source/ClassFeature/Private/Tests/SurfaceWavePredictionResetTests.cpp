#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWCharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurfaceWavePredictionResetTest,
	"ArtisticSW.Swimming.Network.SurfaceWavePredictionReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSurfaceWavePredictionResetTest::RunTest(const FString& Parameters)
{
	USWCharacterMovementComponent* Movement = NewObject<USWCharacterMovementComponent>();
	const auto SeedOldSession = [Movement]()
	{
		Movement->bHasAcceptedSurfaceWaveTimeAnchor = true;
		Movement->LastAcceptedSurfaceWaveServerTimeSeconds = 51.466102;
		Movement->LastAcceptedSurfaceWaveClientTimeStamp = 50.514103f;
		Movement->SetActiveSurfaceWaveServerTime(51.466102);
		Movement->bForceSurfaceWaveCorrectionForCurrentMove = true;
	};
	SeedOldSession();
	TestFalse(TEXT("Old anchor rejects the restarted timestamp from the observed failure"),
		Movement->ValidateSurfaceWaveServerTime(54.154587, 0.018079f, false, 54.195619));
	Movement->ResetPredictionData_Server();
	TestFalse(TEXT("Server reset removes the old anchor"), Movement->bHasAcceptedSurfaceWaveTimeAnchor);
	TestFalse(TEXT("Server reset clears forced correction"), Movement->bForceSurfaceWaveCorrectionForCurrentMove);
	TestTrue(TEXT("New session accepts a current wave time with restarted movement timestamp"),
		Movement->ValidateSurfaceWaveServerTime(54.154587, 0.018079f, false, 54.195619));
	TestFalse(TEXT("Stale wave time is still rejected"), Movement->ValidateSurfaceWaveServerTime(40.0, 0.018079f, false, 54.195619));
	TestFalse(TEXT("Future wave time is still rejected"), Movement->ValidateSurfaceWaveServerTime(60.0, 0.018079f, false, 54.195619));
	SeedOldSession();
	Movement->ResetPredictionData_Client();
	double ActiveTime = 0.0;
	TestFalse(TEXT("Client reset clears active replay wave time"), Movement->TryGetActiveSurfaceWaveServerTime(ActiveTime));
	TestFalse(TEXT("Client reset removes the old anchor"), Movement->bHasAcceptedSurfaceWaveTimeAnchor);
	Movement->bHasAcceptedSurfaceWaveTimeAnchor = true;
	Movement->LastAcceptedSurfaceWaveClientTimeStamp = Movement->MinTimeBetweenTimeStampResets + 10.0f;
	Movement->LastAcceptedSurfaceWaveServerTimeSeconds = 310.0;
	Movement->OnClientTimeStampResetDetected();
	TestTrue(TEXT("Periodic CMC timestamp wrap retains the anchor"), Movement->bHasAcceptedSurfaceWaveTimeAnchor);
	TestTrue(TEXT("Periodic wrap still validates matching deltas"), Movement->ValidateSurfaceWaveServerTime(311.0, 11.0f, false, 311.0));
	return true;
}

#endif
