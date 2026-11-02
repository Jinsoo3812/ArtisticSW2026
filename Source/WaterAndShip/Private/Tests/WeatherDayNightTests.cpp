#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SWNetworkWeatherActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherDayNightRatioTest, "ArtisticSW.Weather.DayNightRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeatherDayNightRatioTest::RunTest(const FString& Parameters)
{
	for (const auto& Bounds : {FVector2D(6, 18), FVector2D(8, 17), FVector2D(5.142857, 18.857142)})
	{
		double Day = 0., Night = 0.;
		for (int32 Hour = 0; Hour < 24; ++Hour)
		{
			const double Seconds = ASWNetworkWeatherActor::CalculateHourDuration(Hour, 10., Bounds.X, Bounds.Y, 0.8);
			const double Overlap = FMath::Max(0., FMath::Min(Hour + 1., Bounds.Y) - FMath::Max(static_cast<double>(Hour), Bounds.X));
			Day += Seconds * Overlap;
			Night += Seconds * (1. - Overlap);
		}
		TestTrue(TEXT("Full day retains its authored 240-second duration"), FMath::IsNearlyEqual(Day + Night, 240.));
		TestTrue(TEXT("Day takes 80 percent"), FMath::IsNearlyEqual(Day, 192.));
		TestTrue(TEXT("Night takes 20 percent"), FMath::IsNearlyEqual(Night, 48.));
	}
	TestEqual(TEXT("Frozen authored clock stays frozen"), ASWNetworkWeatherActor::CalculateHourDuration(12, 0., 6., 18., 0.8), 0.);
	TestEqual(TEXT("Invalid sunrise/sunset retains original speed"), ASWNetworkWeatherActor::CalculateHourDuration(12, 10., 18., 6., 0.8), 10.);
	return true;
}
#endif
