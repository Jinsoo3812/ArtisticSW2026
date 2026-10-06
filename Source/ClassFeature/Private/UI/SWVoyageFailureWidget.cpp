#include "UI/SWVoyageFailureWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void USWVoyageFailureWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.025f, 0.035f, 0.045f, 0.98f));
	Panel->SetPadding(FMargin(32.f));
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);
	PanelSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Layout);
	MessageText = WidgetTree->ConstructWidget<UTextBlock>();
	MessageText->SetText(FText::FromString(TEXT("항해 준비에 실패했습니다.")));
	MessageText->SetAutoWrapText(true);
	MessageText->SetWrapTextAt(560.f);
	Layout->AddChildToVerticalBox(MessageText)->SetPadding(FMargin(0.f, 0.f, 0.f, 20.f));
	WaitingText = WidgetTree->ConstructWidget<UTextBlock>();
	WaitingText->SetText(FText::FromString(TEXT("호스트의 재시도를 기다리는 중입니다.")));
	Layout->AddChildToVerticalBox(WaitingText)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	RetryButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* RetryLabel = WidgetTree->ConstructWidget<UTextBlock>();
	RetryLabel->SetText(FText::FromString(TEXT("재시도")));
	RetryButton->SetContent(RetryLabel);
	RetryButton->OnClicked.AddDynamic(this, &USWVoyageFailureWidget::HandleRetry);
	Layout->AddChildToVerticalBox(RetryButton)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	UButton* LeaveButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* LeaveLabel = WidgetTree->ConstructWidget<UTextBlock>();
	LeaveLabel->SetText(FText::FromString(TEXT("방 나가기")));
	LeaveButton->SetContent(LeaveLabel);
	LeaveButton->OnClicked.AddDynamic(this, &USWVoyageFailureWidget::HandleLeave);
	Layout->AddChildToVerticalBox(LeaveButton);
}

void USWVoyageFailureWidget::Configure(bool bHost, const FString& Message, FSimpleDelegate InRetry, FSimpleDelegate InLeave)
{
	RetryAction = MoveTemp(InRetry); LeaveAction = MoveTemp(InLeave);
	if (MessageText) MessageText->SetText(FText::FromString(Message.Left(512)));
	if (RetryButton)
	{
		RetryButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		RetryButton->SetIsEnabled(bHost && RetryAction.IsBound());
	}
	if (WaitingText) WaitingText->SetVisibility(bHost ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void USWVoyageFailureWidget::HandleRetry()
{
	if (!RetryAction.IsBound()) return;
	RetryButton->SetIsEnabled(false);
	RetryAction.Execute();
}

void USWVoyageFailureWidget::HandleLeave()
{
	LeaveAction.ExecuteIfBound();
}

void USWVoyageFailureWidget::NativeDestruct()
{
	RetryAction.Unbind(); LeaveAction.Unbind();
	Super::NativeDestruct();
}
