#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Audio/SWWorldAudio.h"
#include "Audio/SWFoleyAudioProfile.h"
#include "Components/SWFoleyComponent.h"
#include "GameFramework/Character.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapePhysicalMaterialOutput.h"
#include "Misc/DataValidation.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWWorldAudioAssetsTest, "SW.Audio.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWWorldAudioAssetsTest::RunTest(const FString&)
{
	auto* Profile = LoadObject<USWWorldAudioProfile>(nullptr, TEXT("/Game/Audio/World/DA_WorldAudio.DA_WorldAudio"));
	if (!TestNotNull(TEXT("Cook root profile loads without source packs"), Profile)) { return false; }
	FDataValidationContext Context;
	TestEqual(TEXT("World profile validates"), Profile->IsDataValid(Context), EDataValidationResult::Valid);
	for (ESWWorldSound Event : { ESWWorldSound::CannonFire, ESWWorldSound::CannonWaterImpact, ESWWorldSound::WaterBombBurst,
		ESWWorldSound::WaterEnter, ESWWorldSound::WaterExit, ESWWorldSound::SwimStroke, ESWWorldSound::UnderwaterStroke })
	{
		const FSWWorldSoundSet* Set = Profile->Events.Find(Event);
		if (!TestNotNull(TEXT("Required positional event"), Set)) { continue; }
		TestNotNull(TEXT("Spatial attenuation"), Set->Attenuation.Get());
		TestNotNull(TEXT("Shared bounded concurrency"), Set->Concurrency.Get());
		if (Set->Concurrency) { TestTrue(TEXT("Event voice limit is bounded"), Set->Concurrency->Concurrency.MaxCount > 0 && Set->Concurrency->Concurrency.MaxCount <= 16); }
		USoundCue* Cue = Cast<USoundCue>(Set->Sound);
		if (!TestNotNull(TEXT("One-shot variation cue"), Cue)) { continue; }
		TestFalse(TEXT("One-shots never loop"), Cue->IsLooping());
		TArray<USoundNodeWavePlayer*> Players;
		Cue->RecursiveFindNode(Cue->FirstNode, Players);
		TestTrue(TEXT("Multiple recordings per event"), Players.Num() >= 2);
		for (USoundNodeWavePlayer* Player : Players)
		{
			Player->LoadAsset(false);
			if (const USoundWave* Wave = Player->GetSoundWave())
			{
				TestEqual(TEXT("Positional source uses mono"), Wave->NumChannels, 1);
				TestTrue(TEXT("Nonempty recording"), Wave->Duration > 0.02f);
				TestFalse(TEXT("No new-project dependency"), Wave->GetPathName().Contains(TEXT("/WaterSFX/")) || Wave->GetPathName().Contains(TEXT("/Footstep_Essentials/")));
			}
			else { AddError(TEXT("Cue wave dependency failed to load")); }
		}
	}
	for (ESWWorldSound Event : { ESWWorldSound::Ocean, ESWWorldSound::Shore, ESWWorldSound::Underwater })
	{
		const FSWWorldSoundSet* Set = Profile->Events.Find(Event);
		TestTrue(TEXT("Ambience is a looping recording"), Set && Set->Sound && Set->Sound->IsLooping());
	}
	if (TestNotNull(TEXT("Underwater mix"), Profile->UnderwaterMix.Get()))
	{
		TestEqual(TEXT("Project Foley/SFX and weather are filtered"), Profile->UnderwaterMix->SoundClassEffects.Num(), 3);
		for (const auto& Adjuster : Profile->UnderwaterMix->SoundClassEffects)
		{
			TestTrue(TEXT("Submerged SFX low-pass is audible"), Adjuster.LowPassFilterFrequency >= 500.f && Adjuster.LowPassFilterFrequency <= 3000.f);
		}
	}
	const auto* Rain = LoadObject<USoundWave>(nullptr, TEXT("/Game/StylizedWeather/EpicContents/Rain_Heavy_Ext01"));
	const auto* Weather = LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/World/Mix/SC_Weather"));
	const auto* FoleyClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/World/Mix/SC_Foley"));
	if (TestNotNull(TEXT("Weather recording"), Rain) && TestNotNull(TEXT("Controlled weather class"), Weather) && TestNotNull(TEXT("Foley mix class"), FoleyClass))
	{
		TestEqual(TEXT("The sound wave used by the weather BP goes through its mix class"), Rain->SoundClassObject.Get(), const_cast<USoundClass*>(Weather));
		TestEqual(TEXT("Weather cannot bypass the final gameplay mix"), Weather->Properties.DefaultSubmix, FoleyClass->Properties.DefaultSubmix);
		TestTrue(TEXT("Weather has lower voice priority than gameplay contacts"), Rain->Priority < 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWWorldAudioSurfaceAssetsTest, "SW.Audio.SurfaceAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWWorldAudioSurfaceAssetsTest::RunTest(const FString&)
{
	auto* Foley = LoadObject<USWFoleyAudioProfile>(nullptr, TEXT("/Game/Audio/Foley/DA_Mannequin_Foley.DA_Mannequin_Foley"));
	if (!TestNotNull(TEXT("Shared player/NPC Foley profile"), Foley)) { return false; }
	for (FGameplayTag Event : TArray<FGameplayTag>{ SW::Foley::Walk, SW::Foley::Run, SW::Foley::Jump, SW::Foley::Land })
	{
		const FSWFoleySoundSet* Set = Foley->ResolveEvent(Event);
		if (!TestNotNull(TEXT("Movement event maps"), Set)) { continue; }
		TSet<FSoftObjectPath> Distinct;
		for (int32 Surface = 1; Surface <= 7; ++Surface)
		{
			const auto Sound = Set->ResolveSound(EPhysicalSurface(Surface));
			TestFalse(TEXT("Surface cue assigned"), Sound.IsNull());
			Distinct.Add(Sound.ToSoftObjectPath());
		}
		TestEqual(TEXT("Seven genuinely distinct surface sound sets"), Distinct.Num(), 7);
	}
	UClass* EnemyClass = LoadClass<ACharacter>(nullptr, TEXT("/Script/Enemy.BaseEnemy"));
	if (TestNotNull(TEXT("Enemy class"), EnemyClass))
	{
		const auto* NPC = EnemyClass->GetDefaultObject<ACharacter>()->FindComponentByClass<USWFoleyComponent>();
		TestTrue(TEXT("Native enemies inherit authored contacts, water entry and separate movement boundaries"), NPC && !NPC->bUseDistanceDrivenSteps && NPC->bPollMovementForJumpAndLand && NPC->bDetectWaterWithoutSwimming);
	}
	auto* Original = LoadObject<UMaterial>(nullptr, TEXT("/Game/MT/Landscape/M_Landscape.M_Landscape"));
	auto* Copy = LoadObject<UMaterial>(nullptr, TEXT("/Game/Audio/World/Surfaces/M_Landscape_Audio.M_Landscape_Audio"));
	if (TestNotNull(TEXT("Original landscape material"), Original) && TestNotNull(TEXT("Audio material copy"), Copy))
	{
		int32 OriginalOutputs = 0, CopyOutputs = 0;
		for (UMaterialExpression* Expression : Original->GetExpressions()) { OriginalOutputs += Cast<UMaterialExpressionLandscapePhysicalMaterialOutput>(Expression) != nullptr; }
		for (UMaterialExpression* Expression : Copy->GetExpressions())
		{
			if (auto* Output = Cast<UMaterialExpressionLandscapePhysicalMaterialOutput>(Expression))
			{
				++CopyOutputs;
				TestEqual(TEXT("Sand/grass/soil collision weights"), Output->Inputs.Num(), 3);
			}
		}
		TestEqual(TEXT("Original material remains unmodified"), OriginalOutputs, 0);
		TestEqual(TEXT("One physical output on the copy"), CopyOutputs, 1);
	}
	return true;
}
#endif
