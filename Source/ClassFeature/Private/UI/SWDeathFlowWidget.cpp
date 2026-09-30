#include "UI/SWDeathFlowWidget.h"
#include "BasePlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void USWDeathFlowWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 SetIsFocusable(true);
 UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
 WidgetTree->RootWidget = Root;
 auto MakeText = [this](const FString& Text, int32 Size)
 {
  UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
  Block->SetText(FText::FromString(Text));
  FSlateFontInfo Font = Block->GetFont(); Font.Size = Size; Block->SetFont(Font);
  Block->SetColorAndOpacity(FSlateColor(FLinearColor::White));
  Block->SetJustification(ETextJustify::Center);
  return Block;
 };
 WaitingText = MakeText(TEXT(""), 28);
 UOverlaySlot* WaitingSlot = Root->AddChildToOverlay(WaitingText);
 WaitingSlot->SetHorizontalAlignment(HAlign_Center); WaitingSlot->SetVerticalAlignment(VAlign_Bottom);
 WaitingSlot->SetPadding(FMargin(0, 0, 0, 100));
 GameOverPanel = WidgetTree->ConstructWidget<UBorder>();
 GameOverPanel->SetBrushColor(FLinearColor::Black);
 GameOverPanel->SetHorizontalAlignment(HAlign_Center); GameOverPanel->SetVerticalAlignment(VAlign_Center);
 UOverlaySlot* PanelSlot = Root->AddChildToOverlay(GameOverPanel);
 PanelSlot->SetHorizontalAlignment(HAlign_Fill); PanelSlot->SetVerticalAlignment(VAlign_Fill);
 UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(); GameOverPanel->SetContent(Stack);
 auto Add = [Stack](UWidget* Child)
 {
  UVerticalBoxSlot* Slot = Stack->AddChildToVerticalBox(Child);
  Slot->SetHorizontalAlignment(HAlign_Center); Slot->SetPadding(FMargin(0, 12));
 };
 Add(MakeText(TEXT("게임 오버"), 48));
 Add(MakeText(TEXT("플레이어 배가 침몰했습니다"), 24));
 USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>();
 ButtonSize->SetWidthOverride(240); ButtonSize->SetHeightOverride(64);
 RetryButton = WidgetTree->ConstructWidget<UButton>();
 RetryButton->AddChild(MakeText(TEXT("다시하기"), 24)); ButtonSize->SetContent(RetryButton); Add(ButtonSize);
 RetryButton->OnClicked.AddDynamic(this, &USWDeathFlowWidget::RetryClicked);
 StatusText = MakeText(TEXT(""), 20); Add(StatusText);
}
void USWDeathFlowWidget::UpdateFlow(bool bGameOver, bool bHost, bool bBusy, const FString& Waiting, const FString& Status)
{
 SetVisibility(bGameOver ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
 WaitingText->SetVisibility(bGameOver ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
 WaitingText->SetText(FText::FromString(Waiting));
 GameOverPanel->SetVisibility(bGameOver ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 RetryButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 RetryButton->SetIsEnabled(bHost && !bBusy);
 StatusText->SetText(FText::FromString(bHost ? Status : TEXT("호스트가 다시 시작하기를 기다리는 중입니다")));
}
void USWDeathFlowWidget::FocusRetry() { RetryButton->SetUserFocus(GetOwningPlayer()); }
void USWDeathFlowWidget::RetryClicked()
{
 if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwningPlayer())) Controller->RequestGameOverRetry();
}
