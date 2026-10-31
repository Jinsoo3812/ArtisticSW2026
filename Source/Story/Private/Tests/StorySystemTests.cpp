#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "GameplayTagContainer.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "StoryDefinition.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"
#include "StorySaveGame.h"

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
	TestTrue(TEXT("Recon preflight succeeds directly from GameStarted"),
		Facade->CanCompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("Recon quest completes directly from GameStarted"),
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
	TestTrue(TEXT("Suppression quest can complete directly after Boss 2 defeat"),
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
	FStoryLegacyDefinitionTest,
	"ArtisticSW.Story.LegacyDefinitionIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryLegacyDefinitionTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	UStorySubsystem* Story = NewObject<UStorySubsystem>(Instance);
	const FGameplayTag Trigger = StoryTests::Tag(TEXT("Story.Test.Fact.Trigger"));
	const FGameplayTag Counter = StoryTests::Tag(TEXT("Story.Test.Counter.Kills"));
	Story->AddFact(Trigger);
	Story->SetCounter(Counter, 7);
	UStoryDefinition* Legacy = NewObject<UStoryDefinition>();
	TestFalse(TEXT("Legacy definition configuration is rejected"), Story->ConfigureDefinition(Legacy));
	TestNull(TEXT("No runtime definition is retained"), Story->GetDefinition());
	TestTrue(TEXT("Rejected legacy configuration preserves progress"), Story->HasFact(Trigger));
	UStorySaveGame* Save = Story->BuildRoomProgress();
	if (!TestNotNull(TEXT("Progress capture needs no definition"), Save)) return false;
	TestFalse(TEXT("New saves have no definition ID"), Save->StoryDefinitionId.IsValid());
	Save->StoryDefinitionId = FPrimaryAssetId(FName(TEXT("StoryDefinition")), FName(TEXT("RetiredCampaign")));
	Story->ResetProgress();
	TestTrue(TEXT("Legacy definition IDs do not block room restoration"), Story->ApplyRoomProgress(Save));
	TestTrue(TEXT("Legacy save restores facts"), Story->HasFact(Trigger));
	TestEqual(TEXT("Legacy save restores counters"), Story->GetCounter(Counter), 7);
	Save->SaveVersion = 999;
	TestFalse(TEXT("Unsupported save versions remain rejected"), Story->ApplyRoomProgress(Save));
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

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* Story = NewObject<UStorySubsystem>(GameInstance);
	TestTrue(TEXT("Progress resets without a definition"), Story->ResetProgress());

	Story->AddFact(Trigger);
	Story->AddFact(FirstState);
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
	TestTrue(TEXT("Load restores explicit story state"), Story->HasFact(FirstState));
	TestEqual(TEXT("Load restores shared counter"), Story->GetCounter(KillCounter), 7);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}


#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoryDevelopmentCheckpointTest,
 "ArtisticSW.Story.DevelopmentDialogueCheckpoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStoryDevelopmentCheckpointTest::RunTest(const FString& Parameters)
{
 UGameInstance* GI = NewObject<UGameInstance>();
 UStorySubsystem* Story = NewObject<UStorySubsystem>(GI);
 UStoryFacadeSubsystem* Facade = NewObject<UStoryFacadeSubsystem>(GI);
 Facade->ConfigureForUseCase(Story);
 Facade->StartNewCampaign();
 FString Error;
 TestNull(TEXT("Unknown target rejected"), Facade->BuildDevelopmentCheckpoint(TEXT("Unknown"), Error));
 const FName Targets[] = { TEXT("Recon"), TEXT("SupplyPatrol"), TEXT("Decipher"), TEXT("Suppress"), TEXT("Uldolmok"), TEXT("Ending") };
 const EStoryNode Nodes[] = { EStoryNode::ReconQuestAccepted, EStoryNode::SupplyPatrolQuestAccepted,
  EStoryNode::DecipherQuestAccepted, EStoryNode::SuppressJapaneseForcesQuestAccepted,
  EStoryNode::UldolmokBattleQuestAccepted, EStoryNode::EndingDialogueCompleted };
 const EStoryNode NextBoss[] = { EStoryNode::MiddleBoss1Defeated, EStoryNode::MiddleBoss2Defeated,
  EStoryNode::MiddleBoss3Defeated, EStoryNode::MiddleBoss3Defeated, EStoryNode::FinalBossDefeated };
 for (int32 Index = 0; Index < UE_ARRAY_COUNT(Targets); ++Index)
 {
  Facade->StartNewCampaign();
  UStorySaveGame* Checkpoint = Facade->BuildDevelopmentCheckpoint(Targets[Index], Error);
  if (!TestNotNull(*FString::Printf(TEXT("%s prepares without a definition"), *Targets[Index].ToString()), Checkpoint)) return false;
  TestFalse(TEXT("Preparing does not mutate live campaign"), Facade->IsStoryNodeReached(Nodes[Index]));
  TestFalse(TEXT("Checkpoint needs no definition ID"), Checkpoint->StoryDefinitionId.IsValid());
  TestTrue(TEXT("Checkpoint restores without a definition"), Story->ApplyRoomProgress(Checkpoint));
  TestTrue(TEXT("Requested dialogue is reached"), Facade->IsStoryNodeReached(Nodes[Index]));
  if (Index < UE_ARRAY_COUNT(NextBoss)) TestFalse(TEXT("Next boss remains alive"), Facade->IsStoryNodeReached(NextBoss[Index]));
  UStorySaveGame* Repeated = Facade->BuildDevelopmentCheckpoint(Targets[Index], Error);
  TestNotNull(TEXT("Current checkpoint can be reapplied"), Repeated);
  if (Index > 0) TestNotNull(TEXT("Earlier dialogue can be selected"), Facade->BuildDevelopmentCheckpoint(Targets[Index - 1], Error));
 }
 Facade->StartNewCampaign();
 UStorySaveGame* Prepared = Facade->BuildDevelopmentCheckpoint(TEXT("Uldolmok"), Error);
 if (!TestNotNull(TEXT("Final dialogue checkpoint prepared"), Prepared)) return false;
 TestFalse(TEXT("Preparing a checkpoint does not mutate live story"), Facade->IsStoryNodeReached(EStoryNode::ReconQuestAccepted));
 TestTrue(TEXT("Prepared checkpoint can be restored"), Story->ApplyRoomProgress(Prepared));
 const EStoryNode Required[] = { EStoryNode::CipherBookAcquired, EStoryNode::CurrentGeneratorUnlocked,
  EStoryNode::WaterBombUnlocked, EStoryNode::BombardmentUnlocked, EStoryNode::MiddleBoss3Defeated,
  EStoryNode::DecipherQuestAccepted, EStoryNode::UldolmokBattleQuestAccepted };
 for (EStoryNode Node : Required) TestTrue(TEXT("Prior main and branch nodes restored"), Facade->IsStoryNodeReached(Node));
 TestFalse(TEXT("Final boss remains alive"), Facade->IsStoryNodeReached(EStoryNode::FinalBossDefeated));
 TestNotNull(TEXT("Current final battle checkpoint can be reapplied"), Facade->BuildDevelopmentCheckpoint(TEXT("Uldolmok"), Error));
 TestNotNull(TEXT("Backward checkpoint accepted"), Facade->BuildDevelopmentCheckpoint(TEXT("Recon"), Error));
 Prepared = Facade->BuildDevelopmentCheckpoint(TEXT("Ending"), Error);
 if (!TestNotNull(TEXT("Ending can follow final checkpoint"), Prepared)) return false;
 TestTrue(TEXT("Ending restored"), Story->ApplyRoomProgress(Prepared));
 TestTrue(TEXT("Ending includes final defeat"), Facade->IsStoryNodeReached(EStoryNode::FinalBossDefeated));
 TestTrue(TEXT("Ending dialogue complete"), Facade->IsStoryNodeReached(EStoryNode::EndingDialogueCompleted));
 for (int32 Index = UE_ARRAY_COUNT(Targets) - 1; Index >= 0; --Index)
 {
  UStorySaveGame* Previous = Story->BuildRoomProgress();
  if (!TestNotNull(TEXT("Previous state captured"), Previous)) return false;
  Previous->Facts.AddTag(StoryTests::TestGranted);
  FStoryCounterValue& OldCounter = Previous->Counters.AddDefaulted_GetRef();
  OldCounter.CounterTag = StoryTests::TestKillCounter; OldCounter.Value = 10;
  Previous->AppliedActionKeys.Add(TEXT("PreviousCampaignAction"));
  TestTrue(TEXT("Previous campaign history seeded"), Story->ApplyRoomProgress(Previous));
  Prepared = Facade->BuildDevelopmentCheckpoint(Targets[Index], Error);
  if (!TestNotNull(TEXT("Any checkpoint can follow later progress"), Prepared)) return false;
  TestFalse(TEXT("Previous facts cleared"), Prepared->Facts.HasTagExact(StoryTests::TestGranted));
  TestTrue(TEXT("Selected state restores"), Story->ApplyRoomProgress(Prepared));
  TestTrue(TEXT("Selected dialogue reached"), Facade->IsStoryNodeReached(Nodes[Index]));
  for (int32 Later = Index + 1; Later < UE_ARRAY_COUNT(Nodes); ++Later)
   TestFalse(TEXT("Later dialogue cleared"), Facade->IsStoryNodeReached(Nodes[Later]));
  if (Index < UE_ARRAY_COUNT(NextBoss)) TestFalse(TEXT("Next boss defeat cleared"), Facade->IsStoryNodeReached(NextBoss[Index]));
  TestTrue(TEXT("Old counters cleared"), Prepared->Counters.IsEmpty());
  TestTrue(TEXT("Old action history cleared"), Prepared->AppliedActionKeys.IsEmpty());
 }
 return true;
}
#endif

#endif
