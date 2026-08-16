#include "UI/NPCDialogueReplyWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/NPCDialogueWidget.h"

void UNPCDialogueReplyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Button_Reply)
	{
		Button_Reply->OnClicked.AddUniqueDynamic(this, &UNPCDialogueReplyWidget::HandleClicked);
	}
}

void UNPCDialogueReplyWidget::NativeDestruct()
{
	if (Button_Reply)
	{
		Button_Reply->OnClicked.RemoveDynamic(this, &UNPCDialogueReplyWidget::HandleClicked);
	}
	OwnerDialogueWidget.Reset();
	Super::NativeDestruct();
}

void UNPCDialogueReplyWidget::InitializeReply(
	UNPCDialogueWidget* InOwnerWidget,
	const FNPCDialogueReply& InReply)
{
	OwnerDialogueWidget = InOwnerWidget;
	ReplyId = InReply.ReplyId;
	if (Text_Reply)
	{
		Text_Reply->SetText(InReply.Text);
	}
}

void UNPCDialogueReplyWidget::HandleClicked()
{
	if (UNPCDialogueWidget* DialogueWidget = OwnerDialogueWidget.Get())
	{
		DialogueWidget->SelectReply(ReplyId);
	}
}
