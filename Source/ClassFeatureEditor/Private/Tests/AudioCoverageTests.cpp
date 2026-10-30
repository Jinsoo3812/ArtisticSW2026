#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Audio/SWWorldAudio.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundMix.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWAudioCoverageTest, "SW.Audio.CoverageAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWAudioCoverageTest::RunTest(const FString&)
{
	const auto* Profile = LoadObject<USWWorldAudioProfile>(nullptr, TEXT("/Game/Audio/World/DA_WorldAudio"));
	if (!TestNotNull(TEXT("World sound profile"), Profile)) { return false; }
	const auto* Leak = Profile->Events.Find(ESWWorldSound::ShipLeak);
	if (TestNotNull(TEXT("Repair-state leak loop"), Leak))
	{
		TestTrue(TEXT("Leak source is looping"), Leak->Sound && Leak->Sound->IsLooping());
		TestNotNull(TEXT("Leak is positional"), Leak->Attenuation.Get());
		if (TestNotNull(TEXT("Leak voices are bounded"), Leak->Concurrency.Get())) { TestEqual(TEXT("Three leak voices"), Leak->Concurrency->Concurrency.MaxCount, 3); }
	}
	const auto* Bird = Profile->Events.Find(ESWWorldSound::Seagull);
	if (TestNotNull(TEXT("Sparse coastal wildlife"), Bird))
	{
		TestTrue(TEXT("Bird calls are finite and low priority"), Bird->Sound && !Bird->Sound->IsLooping() && Bird->Sound->Priority < 1.f);
		if (TestNotNull(TEXT("One bird call at a time"), Bird->Concurrency.Get())) { TestEqual(TEXT("Wildlife cannot stack"), Bird->Concurrency->Concurrency.MaxCount, 1); }
	}
	if (TestNotNull(TEXT("Cabin weather mix"), Profile->CabinMix.Get()))
	{
		TestEqual(TEXT("Cabin mix touches one class"), Profile->CabinMix->SoundClassEffects.Num(), 1);
		for (const auto& Effect : Profile->CabinMix->SoundClassEffects)
		{
			TestTrue(TEXT("Rain only; gameplay contacts stay clear"), Effect.SoundClassObject && Effect.SoundClassObject->GetName() == TEXT("SC_Weather"));
			TestTrue(TEXT("Cabin rain is softer and filtered"), Effect.VolumeAdjuster > 0.f && Effect.VolumeAdjuster < 0.5f && Effect.LowPassFilterFrequency < 3000.f);
		}
	}
	return true;
}
#endif
