#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Profiling/SWLevelProfileController.h"
#include "Profiling/SWRippleProfileController.h"
#include "RippleSubsystem.h"
#include "SWRippleWaterWaves.h"
#include "UObject/CoreRedirects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterModulePlacementTest,
	"ArtisticSW.Water.ModulePlacementAndRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterModulePlacementTest::RunTest(const FString& Parameters)
{
	struct FClassExpectation
	{
		const TCHAR* OldPath;
		UClass* NewClass;
	};

	const FClassExpectation Expectations[] =
	{
		{ TEXT("/Script/ClassFeature.RippleSubsystem"), URippleSubsystem::StaticClass() },
		{ TEXT("/Script/ClassFeature.SWRippleWaterWaves"), USWRippleWaterWaves::StaticClass() },
		{ TEXT("/Script/ClassFeature.SWRippleProfileController"), ASWRippleProfileController::StaticClass() },
		{ TEXT("/Script/ClassFeature.SWLevelProfileController"), ASWLevelProfileController::StaticClass() },
	};

	for (const FClassExpectation& Expectation : Expectations)
	{
		TestEqual(
			FString::Printf(TEXT("%s is exported by WaterAndShip"), *Expectation.NewClass->GetName()),
			Expectation.NewClass->GetOutermost()->GetName(),
			FString(TEXT("/Script/WaterAndShip")));

		const FCoreRedirectObjectName RedirectedName = FCoreRedirects::GetRedirectedName(
			ECoreRedirectFlags::Type_Class,
			FCoreRedirectObjectName(FString(Expectation.OldPath)));
		TestEqual(
			FString::Printf(TEXT("%s redirects to the relocated class"), Expectation.OldPath),
			RedirectedName.ToString(),
			FCoreRedirectObjectName(Expectation.NewClass).ToString());
	}

	return true;
}

#endif
