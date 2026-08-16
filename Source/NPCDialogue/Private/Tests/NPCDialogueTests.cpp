#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "NPCDialogueData.h"
#include "NPCDialogueSourceComponent.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/DataValidation.h"

namespace NPCDialogueTests
{
	FNPCDialogueLine MakeLine(FName LineId, const TCHAR* Text)
	{
		FNPCDialogueLine Line;
		Line.LineId = LineId;
		Line.Text = FText::FromString(Text);
		return Line;
	}

	FNPCDialogueRule MakeRule(FName RuleId, int32 Priority, const TCHAR* Text)
	{
		FNPCDialogueRule Rule;
		Rule.RuleId = RuleId;
		Rule.Priority = Priority;
		Rule.Lines.Add(MakeLine(TEXT("Line_01"), Text));
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNPCDialogueDataValidationTest,
	"ArtisticSW.NPCDialogue.DataValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDialogueDataValidationTest::RunTest(const FString& Parameters)
{
	UNPCDialogueData* Data = NewObject<UNPCDialogueData>();
	Data->DisplayName = FText::FromString(TEXT("Test NPC"));
	FNPCDialogueRule ValidRule = NPCDialogueTests::MakeRule(TEXT("Ambient"), 0, TEXT("Hello"));
	ValidRule.Lines.Add(NPCDialogueTests::MakeLine(TEXT("Accepted"), TEXT("Good choice")));
	FNPCDialogueReply Reply;
	Reply.ReplyId = TEXT("Accept");
	Reply.Text = FText::FromString(TEXT("I accept"));
	Reply.NextLineId = TEXT("Accepted");
	ValidRule.Lines[0].Replies.Add(Reply);
	Data->Rules.Add(ValidRule);

	FDataValidationContext ValidContext;
	TestEqual(TEXT("A named NPC with one complete rule validates"),
		Data->IsDataValid(ValidContext), EDataValidationResult::Valid);

	Data->Rules.Add(NPCDialogueTests::MakeRule(TEXT("Ambient"), 100, TEXT("Duplicate")));
	FDataValidationContext InvalidContext;
	TestEqual(TEXT("Duplicate stable rule IDs are rejected"),
		Data->IsDataValid(InvalidContext), EDataValidationResult::Invalid);

	Data->Rules.SetNum(1);
	Data->Rules[0].Lines[0].Replies[0].NextLineId = TEXT("MissingLine");
	FDataValidationContext MissingTargetContext;
	TestEqual(TEXT("A reply cannot target a missing line"),
		Data->IsDataValid(MissingTargetContext), EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNPCDialogueSharedStorySelectionTest,
	"ArtisticSW.NPCDialogue.SharedStorySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDialogueSharedStorySelectionTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UStorySubsystem* StoryState = NewObject<UStorySubsystem>(GameInstance);
	UStoryFacadeSubsystem* Story = NewObject<UStoryFacadeSubsystem>(GameInstance);
	Story->ConfigureForUseCase(StoryState);
	TestTrue(TEXT("Shared campaign starts"), Story->StartNewCampaign());

	UNPCDialogueData* Data = NewObject<UNPCDialogueData>();
	Data->DisplayName = FText::FromString(TEXT("Test NPC"));
	FNPCDialogueRule Intro = NPCDialogueTests::MakeRule(TEXT("SharedIntro"), 100, TEXT("First time only"));
	Intro.RequiredStoryNodes.Add(EStoryNode::GameStarted);
	Intro.bCompleteStoryNode = true;
	Intro.StoryNodeToComplete = EStoryNode::FirstSailingCompleted;
	Intro.bHideAfterStoryCompletion = true;
	Data->Rules.Add(Intro);
	Data->Rules.Add(NPCDialogueTests::MakeRule(TEXT("Ambient"), 0, TEXT("Repeatable")));

	AActor* NPC = NewObject<AActor>();
	UNPCDialogueSourceComponent* Source = NewObject<UNPCDialogueSourceComponent>(NPC);
	Source->SetDialogueData(Data);
	const FNPCDialogueRule* Before = Source->ResolveBestRule(Story, nullptr);
	TestNotNull(TEXT("Story rule resolves before shared completion"), Before);
	if (Before)
	{
		TestEqual(TEXT("High-priority shared intro wins"), Before->RuleId, FName(TEXT("SharedIntro")));
	}

	TestTrue(TEXT("First player completes the shared story node"),
		Story->CompleteStoryNode(EStoryNode::FirstSailingCompleted));
	const FNPCDialogueRule* After = Source->ResolveBestRule(Story, nullptr);
	TestNotNull(TEXT("Fallback remains available to every player"), After);
	if (After)
	{
		TestEqual(TEXT("All players now resolve repeatable ambient dialogue"),
			After->RuleId, FName(TEXT("Ambient")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNPCDialogueExclusiveReservationTest,
	"ArtisticSW.NPCDialogue.ExclusiveReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDialogueExclusiveReservationTest::RunTest(const FString& Parameters)
{
	AActor* NPC = NewObject<AActor>();
	AActor* FirstPlayer = NewObject<AActor>();
	AActor* SecondPlayer = NewObject<AActor>();
	UNPCDialogueSourceComponent* Source = NewObject<UNPCDialogueSourceComponent>(NPC);

	TestTrue(TEXT("First player reserves the NPC"), Source->TryReserve(FirstPlayer));
	TestFalse(TEXT("Second player is rejected while the NPC is reserved"), Source->TryReserve(SecondPlayer));
	TestTrue(TEXT("Reservation belongs to the first player"), Source->IsReservedBy(FirstPlayer));
	Source->Release(FirstPlayer);
	TestTrue(TEXT("Second player can reserve after release"), Source->TryReserve(SecondPlayer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNPCDialogueAuthoredTestAssetTest,
	"ArtisticSW.NPCDialogue.AuthoredTestAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDialogueAuthoredTestAssetTest::RunTest(const FString& Parameters)
{
	UNPCDialogueData* Data = LoadObject<UNPCDialogueData>(
		nullptr,
		TEXT("/Game/New/NPC/Data/DA_TestNPCDialogue.DA_TestNPCDialogue"));
	if (!TestNotNull(TEXT("DA_TestNPCDialogue loads"), Data))
	{
		return false;
	}

	FDataValidationContext Context;
	TestEqual(TEXT("Authored test dialogue validates"),
		Data->IsDataValid(Context), EDataValidationResult::Valid);
	const FNPCDialogueRule* Rule = Data->FindRule(TEXT("Ambient_Default"));
	TestNotNull(TEXT("Test asset contains Ambient_Default"), Rule);
	if (Rule)
	{
		const FNPCDialogueLine* ReplyLine = Rule->Lines.FindByPredicate(
			[](const FNPCDialogueLine& Line)
			{
				return Line.LineId == TEXT("Ambient_02");
			});
		TestNotNull(TEXT("Test asset contains its reply line"), ReplyLine);
		if (ReplyLine)
		{
			TestEqual(TEXT("Reply line contains the two storyboard choices"),
				ReplyLine->Replies.Num(), 2);
		}
	}
	return true;
}

#endif
