#include "UI/NPCDialogueWidget.h"

#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "InputCoreTypes.h"
#include "PlayerDialogueComponent.h"
#include "UI/NPCDialogueReplyWidget.h"

UNPCDialogueWidget::UNPCDialogueWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

FReply UNPCDialogueWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	// A viewport click must not move keyboard focus back to PIE. Keep Escape routed
	// through this widget for the entire dialogue session.
	SetKeyboardFocus();
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && CanAdvanceDirectly())
	{
		if (UPlayerDialogueComponent* Component = DialogueComponent.Get())
		{
			Component->AdvanceDialogue();
			return FReply::Handled();
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UNPCDialogueWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	// Preview receives Escape even when a reply button currently owns focus.
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F)
	{
		if (UPlayerDialogueComponent* Component = DialogueComponent.Get())
		{
			Component->CancelDialogue();
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UNPCDialogueWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::F)
	{
		if (UPlayerDialogueComponent* Component = DialogueComponent.Get())
		{
			Component->CancelDialogue();
			return FReply::Handled();
		}
	}
	if (CanAdvanceDirectly()
		&& (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom))
	{
		if (UPlayerDialogueComponent* Component = DialogueComponent.Get())
		{
			Component->AdvanceDialogue();
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UNPCDialogueWidget::InitializeDialogue(
	UPlayerDialogueComponent* InDialogueComponent,
	const FNPCDialogueView& InView)
{
	DialogueComponent = InDialogueComponent;
	ApplyDialogueView(InView);
}

void UNPCDialogueWidget::ApplyDialogueView(const FNPCDialogueView& InView)
{
	CurrentView = InView;
	if (Text_NPCName)
	{
		Text_NPCName->SetText(CurrentView.NPCDisplayName);
	}
	if (Text_Dialogue)
	{
		Text_Dialogue->SetText(CurrentView.CurrentLine.Text);
	}
	if (Text_ContinueHint)
	{
		Text_ContinueHint->SetVisibility(
			CanAdvanceDirectly() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RebuildReplies();
}

void UNPCDialogueWidget::SelectReply(FName ReplyId)
{
	if (!ReplyId.IsNone())
	{
		if (UPlayerDialogueComponent* Component = DialogueComponent.Get())
		{
			Component->SelectReply(ReplyId);
		}
	}
}

void UNPCDialogueWidget::RebuildReplies()
{
	if (!VerticalBox_Replies)
	{
		return;
	}
	VerticalBox_Replies->ClearChildren();
	VerticalBox_Replies->SetVisibility(
		CurrentView.Replies.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (!ReplyWidgetClass)
	{
		return;
	}

	for (const FNPCDialogueReply& Reply : CurrentView.Replies)
	{
		if (UNPCDialogueReplyWidget* ReplyWidget = CreateWidget<UNPCDialogueReplyWidget>(
			GetOwningPlayer(), ReplyWidgetClass))
		{
			ReplyWidget->InitializeReply(this, Reply);
			VerticalBox_Replies->AddChildToVerticalBox(ReplyWidget);
		}
	}
}
