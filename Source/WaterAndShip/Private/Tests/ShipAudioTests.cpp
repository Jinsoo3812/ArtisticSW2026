#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Audio/SWShipAudioSubsystem.h"
#include "Audio/SWWorldAudio.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWShipAudioTest, "SW.Audio.ShipMotionAndAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWShipAudioTest::RunTest(const FString&)
{
	TestEqual(TEXT("Stationary ships stay silent"), USWShipAudioSubsystem::MovementGain(0), 0.f);
	TestTrue(TEXT("Cruising is stronger than drifting"), USWShipAudioSubsystem::MovementGain(500) > USWShipAudioSubsystem::MovementGain(80));
	TestEqual(TEXT("Extreme speeds remain bounded"), USWShipAudioSubsystem::MovementGain(5000), 1.f);
	TestFalse(TEXT("A streamed/teleported ship does not produce a wake burst"), USWShipAudioSubsystem::IsContinuousMotion(3000, .25));
	TestFalse(TEXT("Old motion snapshots do not resume as fresh movement"), USWShipAudioSubsystem::IsContinuousMotion(100, 2));
	TestTrue(TEXT("Replicated ordinary movement can drive local water sound"), USWShipAudioSubsystem::IsContinuousMotion(125, .25));
	TestTrue(TEXT("An approaching wave can hit a moving bow"), USWShipAudioSubsystem::ShouldPlayWave(250, 100, .25));
	TestFalse(TEXT("Vertical bobbing alone does not produce ship impacts"), USWShipAudioSubsystem::ShouldPlayWave(0, 100, .25));
	TestFalse(TEXT("Receding water does not produce an impact"), USWShipAudioSubsystem::ShouldPlayWave(250, -100, .25));
	TestFalse(TEXT("Stale water samples do not become wave hits"), USWShipAudioSubsystem::ShouldPlayWave(250, 100, 2));
	const auto* Profile = LoadObject<USWWorldAudioProfile>(nullptr, TEXT("/Game/Audio/World/DA_WorldAudio"));
	if (!TestNotNull(TEXT("World sound profile"), Profile)) { return false; }
	const auto* Ship = Profile->Events.Find(ESWWorldSound::ShipMovement);
	if (TestNotNull(TEXT("Player and enemy ships share a ship-water event"), Ship))
	{
		TestTrue(TEXT("Ship water is a loop"), Ship->Sound && Ship->Sound->IsLooping());
		TestNotNull(TEXT("Positional ship attenuation"), Ship->Attenuation.Get());
		if (TestNotNull(TEXT("Bounded ship concurrency"), Ship->Concurrency.Get()))
			TestEqual(TEXT("Four ship water voices per listener"), Ship->Concurrency->Concurrency.MaxCount, 4);
	}
	const auto* Entry = Profile->Events.Find(ESWWorldSound::WaterEnter);
	const auto* Wade = Profile->Events.Find(ESWWorldSound::WaterWadeEnter);
	const auto* Impact = Profile->Events.Find(ESWWorldSound::CannonWaterImpact);
	TestTrue(TEXT("Ankle-depth and full-body water entry have different recordings"), Entry && Wade && Entry->Sound != Wade->Sound);
	TestTrue(TEXT("Character and cannon water entries use distinct impact presentations"), Entry && Impact && Entry->Sound != Impact->Sound);
	return true;
}
#endif
