#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/SWAnimNotify_EnemyFootContact.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "BaseEnemy.h"
#include "Components/SWFoleyComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWEnemyFootContactAssetsTest, "SW.Audio.EnemyAuthoredContacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWEnemyFootContactAssetsTest::RunTest(const FString&)
{
	const auto* Foley = GetDefault<ABaseEnemy>()->FindComponentByClass<USWFoleyComponent>();
	if (!TestNotNull(TEXT("Enemy owns Foley"), Foley)) { return false; }
	TestFalse(TEXT("Authored enemy contacts never overlap distance cadence"), Foley->bUseDistanceDrivenSteps);
	TestTrue(TEXT("Jump/land stays independent of contact notifies"), Foley->bPollMovementForJumpAndLand);
	for (const TCHAR* Gait : { TEXT("Walk"), TEXT("Jog") })
	{
		for (const TCHAR* Direction : { TEXT("Bwd"), TEXT("Bwd_Left"), TEXT("Bwd_Right"), TEXT("Fwd"), TEXT("Fwd_Left"), TEXT("Fwd_Right"), TEXT("Left"), TEXT("Right") })
		{
			const FString Path = FString::Printf(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/%s/MF_Unarmed_%s_%s"), Gait, Gait, Direction);
			const auto* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
			if (!TestNotNull(*Path, Clip)) { continue; }
			int32 Left = 0, Right = 0;
			for (const auto& Event : Clip->Notifies)
			{
				const auto* Contact = Cast<USWAnimNotify_EnemyFootContact>(Event.Notify);
				if (!Contact) { continue; }
				Left += Contact->Side == ESWFoleySide::Left;
				Right += Contact->Side == ESWFoleySide::Right;
				TestTrue(TEXT("Marker lies inside the locomotion clip"), Event.GetTime() >= 0 && Event.GetTime() < Clip->GetPlayLength());
				TestFalse(TEXT("Contact never runs on a dedicated server"), Event.bTriggerOnDedicatedServer);
				TestFalse(TEXT("Sync followers cannot double a contact"), Event.bTriggerOnFollower);
			}
			TestTrue(*FString::Printf(TEXT("%s has paired left/right contacts"), *Path), Left > 0 && Left == Right);
		}
	}
	for (const TCHAR* Path : { TEXT("/Game/Fab/Stylized_Warrior/BS_Warrior"), TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run") })
	{
		const auto* Blend = LoadObject<UBlendSpace>(nullptr, Path);
		if (TestNotNull(TEXT("Enemy locomotion blend space"), Blend))
		{
			TestTrue(TEXT("Only the dominant animation emits contacts"), Blend->NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation);
			bool bWalkingSample = false;
			for (const FBlendSample& Sample : Blend->GetBlendSamples())
				bWalkingSample |= Sample.Animation && Sample.Animation->GetPathName().Contains(TEXT("/Walk/")) && Sample.SampleValue.Y > 0 && Sample.SampleValue.Y <= 150;
			TestTrue(TEXT("Walking at 140cm/s does not select an idle contact clock"), bWalkingSample);
		}
	}
	return true;
}
#endif
