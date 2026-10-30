#include "UI/SWDeathFlowWidget.h"
#include "BasePlayerController.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void USWDeathFlowWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 SetIsFocusable(true);
 if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &USWDeathFlowWidget::RetryClicked);
}
void USWDeathFlowWidget::UpdateFlow(bool bGameOver, bool bHost, bool bBusy, const FString& Waiting, const FString& Status)
{
 SetVisibility(bGameOver ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
 if (PersonalDeathPanel) PersonalDeathPanel->SetVisibility(bGameOver ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
 if (WaitingText) WaitingText->SetText(FText::FromString(Waiting));
 if (GameOverPanel) GameOverPanel->SetVisibility(bGameOver ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 if (RetryButton) RetryButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 if (RetryButton) RetryButton->SetIsEnabled(bHost && !bBusy);
 if (StatusText) StatusText->SetText(FText::FromString(bHost ? (Status.IsEmpty() ? (bBusy ? TEXT("다음 항해를 준비 중입니다...") : TEXT("다시 도전하여 항해를 시작하세요")) : Status) : TEXT("호스트가 다시 시작하기를 기다리는 중입니다")));
}
void USWDeathFlowWidget::FocusRetry() { if (RetryButton && GetOwningPlayer()) RetryButton->SetUserFocus(GetOwningPlayer()); }
void USWDeathFlowWidget::RetryClicked()
{
	UE_LOG(LogTemp, Display, TEXT("[SWLifeDiag] Event=RetryWidgetClicked Widget=%s Controller=%s"), *GetName(), *GetNameSafe(GetOwningPlayer()));
 if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwningPlayer())) Controller->RequestGameOverRetry();
}
