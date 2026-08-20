#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "GameplayTagContainer.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "StoryDefinition.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"

namespace StoryTests
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestTrigger, "Story.Test.Fact.Trigger");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestGranted, "Story.Test.Fact.Granted");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestFirstState, "Story.Test.State.First");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestSecondState, "Story.Test.State.Second");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestKillCounter, "Story.Test.Counter.Kills");

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}

	FGameplayTagQuery MatchAll(FGameplayTag TagToMatch)
	{
		FGameplayTagQueryExpression Expression;
		Expression.AllTagsMatch().AddTag(TagToMatch);
		FGameplayTagQuery Query;
		Query.Build(Expression);
		return Query;
	}

	FStoryStateRule MakeRule(
		FGameplayTag StateTag,
		FGameplayTag RequiredFact,
		FGameplayTag GrantedFact = FGameplayTag())
	{
		FStoryStateRule Rule;
		Rule.StateTag = StateTag;
		Rule.DisplayName = FText::FromString(StateTag.ToString());
		Rule.ActivationCondition.FactQuery = MatchAll(RequiredFact);
		if (GrantedFact.IsValid())
		{
			Rule.GrantedFacts.AddTag(GrantedFact);
		}
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStoryFacadeFlowTest,
	"ArtisticSW.Story.FacadeScreenshotDependencyFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryFacadeFlowTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* Story = NewObject<UStorySubsystem>(GameInstance);
	UStoryFacadeSubsystem* Facade = NewObject<UStoryFacadeSubsystem>(GameInstance);
	Facade->ConfigureForUseCase(Story);

	TestTrue(TEXT("New shared campaign starts"), Facade->StartNewCampaign());
	TestTrue(TEXT("GameStarted is immediately reached"),
		Facade->IsStoryNodeReached(EStoryNode::GameStarted));
	TestFalse(TEXT("Recon cannot complete before first sailing"),
		Facade->CompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestFalse(TEXT("Recon preflight also reports missing prerequisite"),
		Facade->CanCompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("First sailing preflight succeeds"),
		Facade->CanCompleteStoryNode(EStoryNode::FirstSailingCompleted));
	TestTrue(TEXT("First sailing completes"),
		Facade->CompleteStoryNode(EStoryNode::FirstSailingCompleted));
	TestTrue(TEXT("Recon preflight succeeds after first sailing"),
		Facade->CanCompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("Recon quest is accepted after first sailing"),
		Facade->CompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("A reached node remains true"),
		Facade->IsStoryNodeReached(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("Completing an existing node is idempotent"),
		Facade->CompleteStoryNode(EStoryNode::ReconQuestAccepted));

	TestTrue(TEXT("Boss 1 defeat succeeds"),
		Facade->CompleteStoryNode(EStoryNode::MiddleBoss1Defeated));
	TestTrue(TEXT("Supply patrol quest is accepted"),
		Facade->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted));
	TestTrue(TEXT("All systems can query the same supply quest node"),
		Facade->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted));
	TestTrue(TEXT("Current Generator unlock succeeds from the same node"),
		Facade->CompleteStoryNode(EStoryNode::CurrentGeneratorUnlocked));
	TestTrue(TEXT("Boss 2 defeat succeeds from the same node"),
		Facade->CompleteStoryNode(EStoryNode::MiddleBoss2Defeated));
	TestTrue(TEXT("Decipher quest is accepted after Boss 2 defeat"),
		Facade->CompleteStoryNode(EStoryNode::DecipherQuestAccepted));

	TestFalse(TEXT("Suppression quest also needs the independent cipher book branch"),
		Facade->CompleteStoryNode(EStoryNode::SuppressJapaneseForcesQuestAccepted));
	TestTrue(TEXT("Cipher book can be acquired from its independent branch"),
		Facade->CompleteStoryNode(EStoryNode::CipherBookAcquired));
	TestTrue(TEXT("Both arrows now allow the suppression quest"),
		Facade->CompleteStoryNode(EStoryNode::SuppressJapaneseForcesQuestAccepted));
	TestTrue(TEXT("Water Bomb unlock succeeds"),
		Facade->CompleteStoryNode(EStoryNode::WaterBombUnlocked));
	TestTrue(TEXT("Boss 3 defeat succeeds"),
		Facade->CompleteStoryNode(EStoryNode::MiddleBoss3Defeated));
	TestTrue(TEXT("Uldolmok battle quest is accepted"),
		Facade->CompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted));
	TestTrue(TEXT("Bombardment unlock succeeds"),
		Facade->CompleteStoryNode(EStoryNode::BombardmentUnlocked));
	TestTrue(TEXT("Final boss defeat succeeds"),
		Facade->CompleteStoryNode(EStoryNode::FinalBossDefeated));
	TestTrue(TEXT("Ending dialogue completes the campaign"),
		Facade->CompleteStoryNode(EStoryNode::EndingDialogueCompleted));
	TestTrue(TEXT("Ending node is shared state"),
		Facade->IsStoryNodeReached(EStoryNode::EndingDialogueCompleted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStoryConditionTest,
	"ArtisticSW.Story.ConditionFactsAndCounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryConditionTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Trigger = StoryTests::Tag(TEXT("Story.Test.Fact.Trigger"));
	const FGameplayTag KillCounter = StoryTests::Tag(TEXT("Story.Test.Counter.Kills"));

	FStoryConditionSet Condition;
	Condition.FactQuery = StoryTests::MatchAll(Trigger);
	FStoryCounterRequirement& Requirement = Condition.CounterRequirements.AddDefaulted_GetRef();
	Requirement.CounterTag = KillCounter;
	Requirement.Comparison = EStoryCounterComparison::GreaterOrEqual;
	Requirement.Value = 3;

	FGameplayTagContainer Facts;
	TMap<FGameplayTag, int32> Counters;
	TestFalse(TEXT("Missing fact and counter fail"), Condition.IsSatisfied(Facts, Counters));
	Facts.AddTag(Trigger);
	Counters.Add(KillCounter, 2);
	TestFalse(TEXT("Counter below threshold fails"), Condition.IsSatisfied(Facts, Counters));
	Counters[KillCounter] = 3;
	TestTrue(TEXT("Fact and threshold satisfy the condition"), Condition.IsSatisfied(Facts, Counters));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStoryStateChainTest,
	"ArtisticSW.Story.AdditiveStateChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryStateChainTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Trigger = StoryTests::Tag(TEXT("Story.Test.Fact.Trigger"));
	const FGameplayTag Granted = StoryTests::Tag(TEXT("Story.Test.Fact.Granted"));
	const FGameplayTag FirstState = StoryTests::Tag(TEXT("Story.Test.State.First"));
	const FGameplayTag SecondState = StoryTests::Tag(TEXT("Story.Test.State.Second"));

	UStoryDefinition* Definition = NewObject<UStoryDefinition>();
	Definition->StateRules.Add(StoryTests::MakeRule(FirstState, Trigger, Granted));
	Definition->StateRules.Add(StoryTests::MakeRule(SecondState, FirstState));

	TArray<FText> Errors;
	TestTrue(TEXT("Valid definition passes validation"), Definition->ValidateDefinition(Errors));

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* Story = NewObject<UStorySubsystem>(GameInstance);
	TestTrue(TEXT("Definition config succeeds"), Story->ConfigureDefinition(Definition, true));
	TestFalse(TEXT("First state starts inactive"), Story->HasFact(FirstState));

	TestTrue(TEXT("Adding a new shared fact changes progress"), Story->AddFact(Trigger));
	TestTrue(TEXT("First state activates"), Story->HasFact(FirstState));
	TestTrue(TEXT("First state grants its fact"), Story->HasFact(Granted));
	TestTrue(TEXT("Dependent state activates in the same evaluation"), Story->HasFact(SecondState));
	TestFalse(TEXT("Adding an existing fact is idempotent"), Story->AddFact(Trigger));

	Definition->StateRules.Add(StoryTests::MakeRule(FirstState, Trigger));
	TestFalse(TEXT("Duplicate state tags fail validation"), Definition->ValidateDefinition(Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStoryPersistenceTest,
	"ArtisticSW.Story.SharedProgressPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryPersistenceTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Trigger = StoryTests::Tag(TEXT("Story.Test.Fact.Trigger"));
	const FGameplayTag FirstState = StoryTests::Tag(TEXT("Story.Test.State.First"));
	const FGameplayTag KillCounter = StoryTests::Tag(TEXT("Story.Test.Counter.Kills"));

	UStoryDefinition* Definition = NewObject<UStoryDefinition>();
	Definition->StateRules.Add(StoryTests::MakeRule(FirstState, Trigger));

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* Story = NewObject<UStorySubsystem>(GameInstance);
	if (!TestTrue(TEXT("Definition config succeeds"), Story->ConfigureDefinition(Definition, true)))
	{
		return false;
	}

	Story->AddFact(Trigger);
	Story->SetCounter(KillCounter, 7);
	const FString Slot = FString::Printf(
		TEXT("StoryAutomation_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TestTrue(TEXT("Shared campaign progress saves"), Story->SaveProgressToSlot(Slot));

	Story->ResetProgress();
	TestFalse(TEXT("Reset removes runtime fact"), Story->HasFact(Trigger));
	TestEqual(TEXT("Reset removes runtime counter"), Story->GetCounter(KillCounter), 0);
	TestTrue(TEXT("Shared campaign progress loads"), Story->LoadProgressFromSlot(Slot));
	TestTrue(TEXT("Load restores source fact"), Story->HasFact(Trigger));
	TestTrue(TEXT("Load restores derived story state"), Story->HasFact(FirstState));
	TestEqual(TEXT("Load restores shared counter"), Story->GetCounter(KillCounter), 7);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#endif
