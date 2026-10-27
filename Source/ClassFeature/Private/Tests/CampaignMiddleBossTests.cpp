#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Misc/AutomationTest.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampaignMiddleBossStageTest,
	"ArtisticSW.Campaign.MiddleBossStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampaignMiddleBossStageTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("One-shot CVar is registered"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Campaign.MiddleBossAndReturn")));
	UGameInstance* Instance = NewObject<UGameInstance>();
	UStorySubsystem* State = NewObject<UStorySubsystem>(Instance);
	UStoryFacadeSubsystem* Story = NewObject<UStoryFacadeSubsystem>(Instance);
	Story->ConfigureForUseCase(State);
	const EStoryNode Quests[] = {EStoryNode::ReconQuestAccepted,
		EStoryNode::SupplyPatrolQuestAccepted, EStoryNode::SuppressJapaneseForcesQuestAccepted};
	const EStoryNode Bosses[] = {EStoryNode::MiddleBoss1Defeated,
		EStoryNode::MiddleBoss2Defeated, EStoryNode::MiddleBoss3Defeated};
	const int32 Stages[] = {1, 2, 3, 1, 3, 2};
	for (int32 Stage : Stages)
	{
		TestTrue(TEXT("Stage can be selected in either direction"), Story->ActivateDevelopmentMiddleBoss(Stage));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestEqual(TEXT("Only current/earlier boss quests are accepted"), Story->IsStoryNodeReached(Quests[Index]), Index < Stage);
			TestEqual(TEXT("Current boss is alive; only earlier bosses are completed"), Story->IsStoryNodeReached(Bosses[Index]), Index < Stage - 1);
		}
		TestFalse(TEXT("Final squad remains gated"), Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted));
		TestFalse(TEXT("Ending is cleared"), Story->IsStoryNodeReached(EStoryNode::EndingDialogueCompleted));
	}
	TestFalse(TEXT("Zero is not a stage"), Story->ActivateDevelopmentMiddleBoss(0));
	TestFalse(TEXT("Four is not a middle boss"), Story->ActivateDevelopmentMiddleBoss(4));
	TestTrue(TEXT("Invalid requests leave campaign unchanged"), Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted));
	return true;
}

#endif
