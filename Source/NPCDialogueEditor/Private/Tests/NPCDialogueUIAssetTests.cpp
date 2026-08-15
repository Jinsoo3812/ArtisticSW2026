#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "PlayerDialogueComponent.h"
#include "UI/NPCDialogueReplyWidget.h"
#include "UI/NPCDialogueWidget.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNPCDialogueUIAssetTest,
	"ArtisticSW.NPCDialogue.UI.DesignerAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDialogueUIAssetTest::RunTest(const FString& Parameters)
{
	const TCHAR* MainObjectPath =
		TEXT("/Game/New/NPC/Blueprints/UI/WBP_NPCDialogue.WBP_NPCDialogue");
	const TCHAR* MainClassPath =
		TEXT("/Game/New/NPC/Blueprints/UI/WBP_NPCDialogue.WBP_NPCDialogue_C");
	const TCHAR* ReplyObjectPath =
		TEXT("/Game/New/NPC/Blueprints/UI/WBP_NPCDialogueReply.WBP_NPCDialogueReply");
	const TCHAR* ReplyClassPath =
		TEXT("/Game/New/NPC/Blueprints/UI/WBP_NPCDialogueReply.WBP_NPCDialogueReply_C");

	UWidgetBlueprint* MainBlueprint = LoadObject<UWidgetBlueprint>(nullptr, MainObjectPath);
	UWidgetBlueprint* ReplyBlueprint = LoadObject<UWidgetBlueprint>(nullptr, ReplyObjectPath);
	TestNotNull(TEXT("Main dialogue Widget Blueprint loads"), MainBlueprint);
	TestNotNull(TEXT("Reply Widget Blueprint loads"), ReplyBlueprint);

	for (const UWidgetBlueprint* Blueprint : { MainBlueprint, ReplyBlueprint })
	{
		if (!Blueprint)
		{
			continue;
		}
		TestEqual(TEXT("Designer WBP has no property bindings"), Blueprint->Bindings.Num(), 0);
		for (const UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph)
			{
				TestEqual(TEXT("Designer WBP event graph has no functional nodes"), Graph->Nodes.Num(), 0);
			}
		}
	}

	UClass* MainClass = LoadClass<UNPCDialogueWidget>(nullptr, MainClassPath);
	UClass* ReplyClass = LoadClass<UNPCDialogueReplyWidget>(nullptr, ReplyClassPath);
	TestNotNull(TEXT("Main dialogue generated class loads"), MainClass);
	TestNotNull(TEXT("Reply generated class loads"), ReplyClass);

	if (const UWidgetBlueprintGeneratedClass* MainGeneratedClass =
		Cast<UWidgetBlueprintGeneratedClass>(MainClass))
	{
		const UWidgetTree* Tree = MainGeneratedClass->GetWidgetTreeArchetype();
		TestNotNull(TEXT("Main dialogue compiled widget tree exists"), Tree);
		if (Tree)
		{
			for (const FName Name : {
				FName(TEXT("Text_NPCName")),
				FName(TEXT("Text_Dialogue")),
				FName(TEXT("VerticalBox_Replies")),
				FName(TEXT("Text_ContinueHint")) })
			{
				TestNotNull(FString::Printf(TEXT("Main designer contains %s"), *Name.ToString()),
					Tree->FindWidget(Name));
			}

			const UWidget* DialogueBox = Tree->FindWidget(TEXT("Border_DialogueBox"));
			const UCanvasPanelSlot* DialogueSlot = DialogueBox
				? Cast<UCanvasPanelSlot>(DialogueBox->Slot)
				: nullptr;
			TestNotNull(TEXT("Dialogue box uses an anchored Canvas slot"), DialogueSlot);
			if (DialogueSlot)
			{
				const FAnchors Anchors = DialogueSlot->GetAnchors();
				TestEqual(TEXT("Dialogue box lower-left anchor"),
					Anchors.Minimum, FVector2D(0.06f, 0.72f));
				TestEqual(TEXT("Dialogue box upper-right anchor"),
					Anchors.Maximum, FVector2D(0.94f, 0.94f));
			}

			const UWidget* Replies = Tree->FindWidget(TEXT("VerticalBox_Replies"));
			const UCanvasPanelSlot* RepliesSlot = Replies
				? Cast<UCanvasPanelSlot>(Replies->Slot)
				: nullptr;
			TestNotNull(TEXT("Reply list uses an anchored Canvas slot"), RepliesSlot);
			if (RepliesSlot)
			{
				TestEqual(TEXT("Reply list is attached to the dialogue box upper-right"),
					RepliesSlot->GetAnchors().Minimum, FVector2D(0.94f, 0.72f));
				TestEqual(TEXT("Reply list grows upward and left from that point"),
					RepliesSlot->GetAlignment(), FVector2D(1.0f, 1.0f));
				TestTrue(TEXT("Reply list sizes to its dynamic children"), RepliesSlot->GetAutoSize());
			}
		}
	}

	if (const UWidgetBlueprintGeneratedClass* ReplyGeneratedClass =
		Cast<UWidgetBlueprintGeneratedClass>(ReplyClass))
	{
		const UWidgetTree* Tree = ReplyGeneratedClass->GetWidgetTreeArchetype();
		TestNotNull(TEXT("Reply compiled widget tree exists"), Tree);
		if (Tree)
		{
			TestNotNull(TEXT("Reply designer contains Button_Reply"),
				Tree->FindWidget(TEXT("Button_Reply")));
			TestNotNull(TEXT("Reply designer contains Text_Reply"),
				Tree->FindWidget(TEXT("Text_Reply")));
		}
	}

	if (MainClass && ReplyClass)
	{
		const UNPCDialogueWidget* MainCDO = MainClass->GetDefaultObject<UNPCDialogueWidget>();
		TestEqual(TEXT("Main WBP uses the Designer reply WBP"),
			MainCDO->GetReplyWidgetClass().Get(), ReplyClass);
	}

	UBlueprint* PlayerBlueprint = LoadObject<UBlueprint>(
		nullptr, TEXT("/Game/Blueprints/Player/BP_Player.BP_Player"));
	TestNotNull(TEXT("BP_Player loads"), PlayerBlueprint);
	if (PlayerBlueprint && PlayerBlueprint->GeneratedClass && MainClass)
	{
		const AActor* PlayerCDO = Cast<AActor>(PlayerBlueprint->GeneratedClass->GetDefaultObject());
		const UPlayerDialogueComponent* DialogueComponent = PlayerCDO
			? PlayerCDO->FindComponentByClass<UPlayerDialogueComponent>()
			: nullptr;
		TestNotNull(TEXT("BP_Player owns PlayerDialogueComponent"), DialogueComponent);
		if (DialogueComponent)
		{
			TestEqual(TEXT("BP_Player is connected to WBP_NPCDialogue"),
				DialogueComponent->GetDialogueWidgetClass().Get(), MainClass);
		}
	}

	return true;
}

#endif
