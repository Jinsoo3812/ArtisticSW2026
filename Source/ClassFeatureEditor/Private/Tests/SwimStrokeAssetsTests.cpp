#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/SWAnimNotify_SwimStroke.h"
#include "BasePlayer.h"
#include "Components/SWFoleyComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWAudioSwimAssetsTest, "SW.Audio.SurfaceSwimContacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWAudioSwimAssetsTest::RunTest(const FString&)
{
	const auto* Foley = GetDefault<ABasePlayer>()->FindComponentByClass<USWFoleyComponent>();
	TestTrue(TEXT("Players use authored surface strokes"), Foley && Foley->bUseAnimationDrivenSurfaceStrokes);
	const auto* Blend = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Anim_Logic/BSs/BS_Swim_Surface"));
	if (!TestNotNull(TEXT("Active surface blend space"), Blend)) { return false; }
	TestTrue(TEXT("Only the dominant swim clip fires contacts"), Blend->NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation);
	TSet<const UAnimSequence*> Unique;
	for (const auto& Sample : Blend->GetBlendSamples())
	{
		const auto* Clip = Sample.Animation.Get();
		if (!Clip || Unique.Contains(Clip)) { continue; }
		Unique.Add(Clip);
		int32 Left = 0, Right = 0;
		for (const auto& Event : Clip->Notifies)
		{
			const auto* Notify = Cast<USWAnimNotify_SwimStroke>(Event.Notify);
			if (!Notify) { continue; }
			Left += Notify->Side == ESWFoleySide::Left; Right += Notify->Side == ESWFoleySide::Right;
			TestFalse(TEXT("Dedicated servers cannot emit a catch"), Event.bTriggerOnDedicatedServer);
			TestFalse(TEXT("Follower clips cannot emit an extra catch"), Event.bTriggerOnFollower);
			TestTrue(TEXT("Marker stays within the cycle"), Event.GetTime() >= 0.f && Event.GetTime() < Clip->GetPlayLength());
		}
		const int32 Expected = Clip->GetName() == TEXT("anim_swim_idle_A") ? 0 : 1;
		TestEqual(Clip->GetName() + TEXT(" left catches"), Left, Expected);
		TestEqual(Clip->GetName() + TEXT(" right catches"), Right, Expected);
	}
	TestEqual(TEXT("All nine active surface clips inspected"), Unique.Num(), 9);
	return true;
}
#endif
