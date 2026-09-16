#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NPCDialogueData.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCampaignNonChestAssetsValidationTest,
	"ArtisticSW.Campaign.NonChestAssetsValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampaignNonChestAssetsValidationTest::RunTest(const FString& Parameters)
{
	UNPCDialogueData* YiSunSinDialogue = LoadObject<UNPCDialogueData>(
		nullptr,
		TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue.DA_YiSunSinDialogue"));
	UNPCDialogueData* BaseNpcDialogue = LoadObject<UNPCDialogueData>(
		nullptr,
		TEXT("/Game/Campaign/DataAsset/Dialogue/DA_BaseNPCDialogue.DA_BaseNPCDialogue"));

	if (TestNotNull(TEXT("YiSunSin dialogue remains available"), YiSunSinDialogue))
	{
		TestTrue(TEXT("YiSunSin dialogue contains rules"), !YiSunSinDialogue->Rules.IsEmpty());
	}
	TestNotNull(TEXT("Base NPC dialogue remains available"), BaseNpcDialogue);

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* StoryState = NewObject<UStorySubsystem>(GameInstance);
	UStoryFacadeSubsystem* Story = NewObject<UStoryFacadeSubsystem>(GameInstance);
	Story->ConfigureForUseCase(StoryState);

	TestTrue(TEXT("Campaign starts"), Story->StartNewCampaign());
	TestTrue(TEXT("Recon quest can complete"), Story->CompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("Mid boss 1 can complete"), Story->CompleteStoryNode(EStoryNode::MiddleBoss1Defeated));
	TestTrue(TEXT("Cipher book acquisition can complete"), Story->CompleteStoryNode(EStoryNode::CipherBookAcquired));
	TestTrue(TEXT("Supply patrol can complete"), Story->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted));
	TestTrue(TEXT("Mid boss 2 can complete"), Story->CompleteStoryNode(EStoryNode::MiddleBoss2Defeated));
	TestTrue(TEXT("Decipher quest can complete"), Story->CompleteStoryNode(EStoryNode::DecipherQuestAccepted));
	TestTrue(TEXT("Suppression quest can complete"), Story->CompleteStoryNode(EStoryNode::SuppressJapaneseForcesQuestAccepted));
	TestTrue(TEXT("Mid boss 3 can complete"), Story->CompleteStoryNode(EStoryNode::MiddleBoss3Defeated));
	TestTrue(TEXT("Uldolmok battle can complete"), Story->CompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted));
	TestTrue(TEXT("Final boss can complete"), Story->CompleteStoryNode(EStoryNode::FinalBossDefeated));
	TestTrue(TEXT("Ending can complete"), Story->CompleteStoryNode(EStoryNode::EndingDialogueCompleted));

	return !HasAnyErrors();
}

#endif
