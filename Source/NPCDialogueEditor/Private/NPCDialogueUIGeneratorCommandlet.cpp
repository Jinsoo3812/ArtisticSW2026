#include "NPCDialogueUIGeneratorCommandlet.h"

#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PlayerDialogueComponent.h"
#include "Styling/CoreStyle.h"
#include "UI/NPCDialogueReplyWidget.h"
#include "UI/NPCDialogueWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace NPCDialogueUIGenerator
{
	constexpr const TCHAR* UIPath = TEXT("/Game/New/NPC/Blueprints/UI");
	constexpr const TCHAR* MainAssetName = TEXT("WBP_NPCDialogue");
	constexpr const TCHAR* ReplyAssetName = TEXT("WBP_NPCDialogueReply");

	FSlateFontInfo MakeHangulFont(float Size)
	{
		// Roboto does not contain Hangul. This engine-shipped fallback covers CJK
		// and is available both in the editor and packaged builds.
		return FSlateFontInfo(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"),
			Size);
	}

	void ApplyHangulFonts(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->WidgetTree)
		{
			return;
		}
		if (UTextBlock* Text = Blueprint->WidgetTree->FindWidget<UTextBlock>(TEXT("Text_NPCName")))
		{
			Text->SetFont(MakeHangulFont(30.0f));
		}
		if (UTextBlock* Text = Blueprint->WidgetTree->FindWidget<UTextBlock>(TEXT("Text_Dialogue")))
		{
			Text->SetFont(MakeHangulFont(25.0f));
		}
		if (UTextBlock* Text = Blueprint->WidgetTree->FindWidget<UTextBlock>(TEXT("Text_ContinueHint")))
		{
			Text->SetFont(MakeHangulFont(16.0f));
		}
		if (UTextBlock* Text = Blueprint->WidgetTree->FindWidget<UTextBlock>(TEXT("Text_Reply")))
		{
			Text->SetFont(MakeHangulFont(23.0f));
		}
	}

	UWidgetBlueprint* CreateWidgetBlueprint(const TCHAR* AssetName, UClass* ParentClass)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), UIPath, AssetName, AssetName);
		if (UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = ParentClass;
		return Cast<UWidgetBlueprint>(FAssetToolsModule::GetModule().Get().CreateAsset(
			AssetName,
			UIPath,
			UWidgetBlueprint::StaticClass(),
			Factory));
	}

	void ClearGraphLogic(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return;
		}
		Blueprint->Bindings.Reset();
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph)
			{
				Graph->Modify();
				Graph->Nodes.Reset();
			}
		}
	}

	template <typename WidgetType>
	WidgetType* MakeWidget(UWidgetTree* Tree, FName Name, bool bVariable = false)
	{
		WidgetType* Widget = Tree ? Tree->ConstructWidget<WidgetType>(WidgetType::StaticClass(), Name) : nullptr;
		if (Widget)
		{
			Widget->bIsVariable = bVariable;
		}
		return Widget;
	}

	void BuildReplyDesigner(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->WidgetTree || Blueprint->WidgetTree->RootWidget)
		{
			return;
		}

		UWidgetTree* Tree = Blueprint->WidgetTree;
		USizeBox* Root = MakeWidget<USizeBox>(Tree, TEXT("SizeBox_ReplyRoot"));
		Root->SetWidthOverride(360.0f);
		Root->SetHeightOverride(54.0f);
		Tree->RootWidget = Root;

		UButton* Button = MakeWidget<UButton>(Tree, TEXT("Button_Reply"), true);
		Button->SetBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.12f));
		Button->SetColorAndOpacity(FLinearColor::White);
		Root->AddChild(Button);

		UVerticalBox* Content = MakeWidget<UVerticalBox>(Tree, TEXT("VerticalBox_ReplyContent"));
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->AddChild(Content)))
		{
			ContentSlot->SetPadding(FMargin(12.0f, 7.0f, 8.0f, 5.0f));
		}

		UTextBlock* ReplyText = MakeWidget<UTextBlock>(Tree, TEXT("Text_Reply"), true);
		ReplyText->SetText(NSLOCTEXT("NPCDialogue", "ReplyPreview", "Reply"));
		ReplyText->SetFont(MakeHangulFont(23.0f));
		ReplyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.92f, 1.0f)));
		ReplyText->SetJustification(ETextJustify::Right);
		if (UVerticalBoxSlot* TextSlot = Content->AddChildToVerticalBox(ReplyText))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		USizeBox* UnderlineSize = MakeWidget<USizeBox>(Tree, TEXT("SizeBox_Underline"));
		UnderlineSize->SetHeightOverride(2.0f);
		if (UVerticalBoxSlot* UnderlineSlot = Content->AddChildToVerticalBox(UnderlineSize))
		{
			UnderlineSlot->SetPadding(FMargin(48.0f, 3.0f, 0.0f, 0.0f));
			UnderlineSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		UBorder* Underline = MakeWidget<UBorder>(Tree, TEXT("Border_Underline"));
		Underline->SetBrushColor(FLinearColor(0.82f, 0.66f, 0.32f, 0.9f));
		UnderlineSize->AddChild(Underline);
	}

	void BuildMainDesigner(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->WidgetTree || Blueprint->WidgetTree->RootWidget)
		{
			return;
		}

		UWidgetTree* Tree = Blueprint->WidgetTree;
		UCanvasPanel* Root = MakeWidget<UCanvasPanel>(Tree, TEXT("CanvasPanel_Root"));
		Tree->RootWidget = Root;

		UBorder* DialogueBox = MakeWidget<UBorder>(Tree, TEXT("Border_DialogueBox"));
		DialogueBox->SetBrushColor(FLinearColor(0.015f, 0.022f, 0.035f, 0.88f));
		DialogueBox->SetPadding(FMargin(36.0f, 24.0f, 36.0f, 20.0f));
		if (UCanvasPanelSlot* DialogueSlot = Root->AddChildToCanvas(DialogueBox))
		{
			DialogueSlot->SetAnchors(FAnchors(0.06f, 0.72f, 0.94f, 0.94f));
			DialogueSlot->SetOffsets(FMargin(0.0f));
		}

		UVerticalBox* DialogueContent = MakeWidget<UVerticalBox>(Tree, TEXT("VerticalBox_Dialogue"));
		DialogueBox->AddChild(DialogueContent);

		UTextBlock* NPCName = MakeWidget<UTextBlock>(Tree, TEXT("Text_NPCName"), true);
		NPCName->SetText(NSLOCTEXT("NPCDialogue", "NPCNamePreview", "NPC 이름"));
		NPCName->SetFont(MakeHangulFont(30.0f));
		NPCName->SetColorAndOpacity(FSlateColor(FLinearColor(0.87f, 0.71f, 0.36f, 1.0f)));
		NPCName->SetShadowOffset(FVector2D(1.0f, 1.0f));
		NPCName->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		if (UVerticalBoxSlot* NameSlot = DialogueContent->AddChildToVerticalBox(NPCName))
		{
			NameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		}

		UTextBlock* DialogueText = MakeWidget<UTextBlock>(Tree, TEXT("Text_Dialogue"), true);
		DialogueText->SetText(NSLOCTEXT("NPCDialogue", "DialoguePreview", "실제 NPC 대화 문장이 이곳에 표시됩니다."));
		DialogueText->SetFont(MakeHangulFont(25.0f));
		DialogueText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.94f, 1.0f)));
		DialogueText->SetAutoWrapText(true);
		DialogueText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		DialogueText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		if (UVerticalBoxSlot* DialogueTextSlot = DialogueContent->AddChildToVerticalBox(DialogueText))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			DialogueTextSlot->SetSize(FillSize);
			DialogueTextSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		UTextBlock* ContinueHint = MakeWidget<UTextBlock>(Tree, TEXT("Text_ContinueHint"), true);
		ContinueHint->SetText(NSLOCTEXT("NPCDialogue", "ContinueHint", "클릭하여 계속  ▶"));
		ContinueHint->SetFont(MakeHangulFont(16.0f));
		ContinueHint->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.74f, 0.76f, 0.9f)));
		ContinueHint->SetJustification(ETextJustify::Right);
		if (UVerticalBoxSlot* ContinueSlot = DialogueContent->AddChildToVerticalBox(ContinueHint))
		{
			ContinueSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			ContinueSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		UVerticalBox* Replies = MakeWidget<UVerticalBox>(Tree, TEXT("VerticalBox_Replies"), true);
		if (UCanvasPanelSlot* RepliesSlot = Root->AddChildToCanvas(Replies))
		{
			RepliesSlot->SetAnchors(FAnchors(0.94f, 0.72f));
			RepliesSlot->SetAlignment(FVector2D(1.0f, 1.0f));
			RepliesSlot->SetAutoSize(true);
			RepliesSlot->SetPosition(FVector2D::ZeroVector);
		}
	}

	bool SaveBlueprint(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return false;
		}
		Blueprint->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
	}

	bool CompileAndSave(UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return false;
		}
		ClearGraphLogic(Blueprint);
		Blueprint->Modify();
		Blueprint->MarkPackageDirty();
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			return false;
		}

		return SaveBlueprint(Blueprint);
	}

	bool ConnectPlayerBlueprint(const TCHAR* ObjectPath, UClass* DialogueWidgetClass)
	{
		UBlueprint* PlayerBlueprint = LoadObject<UBlueprint>(nullptr, ObjectPath);
		if (!PlayerBlueprint || !PlayerBlueprint->GeneratedClass)
		{
			return false;
		}
		AActor* PlayerCDO = Cast<AActor>(PlayerBlueprint->GeneratedClass->GetDefaultObject());
		UPlayerDialogueComponent* DialogueComponent = PlayerCDO
			? PlayerCDO->FindComponentByClass<UPlayerDialogueComponent>()
			: nullptr;
		if (!DialogueComponent)
		{
			return false;
		}
		DialogueComponent->Modify();
		DialogueComponent->SetDialogueWidgetClass(DialogueWidgetClass);
		PlayerBlueprint->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(
			PlayerBlueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(PlayerBlueprint->GetOutermost(), PlayerBlueprint, *Filename, SaveArgs);
	}
}

UNPCDialogueUIGeneratorCommandlet::UNPCDialogueUIGeneratorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UNPCDialogueUIGeneratorCommandlet::Main(const FString& Params)
{
	using namespace NPCDialogueUIGenerator;

	UWidgetBlueprint* ReplyBlueprint = CreateWidgetBlueprint(
		ReplyAssetName, UNPCDialogueReplyWidget::StaticClass());
	BuildReplyDesigner(ReplyBlueprint);
	ApplyHangulFonts(ReplyBlueprint);
	if (!CompileAndSave(ReplyBlueprint) || !ReplyBlueprint->GeneratedClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to generate WBP_NPCDialogueReply."));
		return 1;
	}

	UWidgetBlueprint* MainBlueprint = CreateWidgetBlueprint(
		MainAssetName, UNPCDialogueWidget::StaticClass());
	BuildMainDesigner(MainBlueprint);
	ApplyHangulFonts(MainBlueprint);
	if (!CompileAndSave(MainBlueprint) || !MainBlueprint->GeneratedClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to generate WBP_NPCDialogue."));
		return 1;
	}

	UNPCDialogueWidget* MainCDO = Cast<UNPCDialogueWidget>(MainBlueprint->GeneratedClass->GetDefaultObject());
	if (!MainCDO)
	{
		return 1;
	}
	MainCDO->Modify();
	MainCDO->SetReplyWidgetClass(
		TSubclassOf<UNPCDialogueReplyWidget>(ReplyBlueprint->GeneratedClass.Get()));
	if (!SaveBlueprint(MainBlueprint))
	{
		return 1;
	}

	const bool bConnectedMainPlayer = ConnectPlayerBlueprint(
		TEXT("/Game/Blueprints/Player/BP_Player.BP_Player"), MainBlueprint->GeneratedClass);
	const bool bConnectedTestPlayer = ConnectPlayerBlueprint(
		TEXT("/Game/Blueprints/Player/BP_BasePlayerTest.BP_BasePlayerTest"), MainBlueprint->GeneratedClass);
	if (!bConnectedMainPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to connect WBP_NPCDialogue to BP_Player."));
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("Generated Designer-only NPC dialogue WBP assets."));
	UE_LOG(LogTemp, Display, TEXT("Connected BP_Player=%s BP_BasePlayerTest=%s"),
		bConnectedMainPlayer ? TEXT("true") : TEXT("false"),
		bConnectedTestPlayer ? TEXT("true") : TEXT("false"));
	return 0;
}
