#include "UI/SWRoomMenuWidget.h"

#include "BasePlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void USWRoomMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomMenuLayout"));
	WidgetTree->RootWidget = Layout;
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("방 메뉴")));
	Layout->AddChildToVerticalBox(Title);
	SaveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SaveButton"));
	UTextBlock* SaveLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SaveLabel"));
	SaveLabel->SetText(FText::FromString(TEXT("저장")));
	SaveButton->AddChild(SaveLabel);
	Layout->AddChildToVerticalBox(SaveButton);
	SaveButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleSaveClicked);
	SaveAndExitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SaveAndExitButton"));
	UTextBlock* SaveAndExitLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SaveAndExitLabel"));
	SaveAndExitLabel->SetText(FText::FromString(TEXT("저장 후 종료")));
	SaveAndExitButton->AddChild(SaveAndExitLabel);
	Layout->AddChildToVerticalBox(SaveAndExitButton);
	SaveAndExitButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleSaveAndExitClicked);
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Status"));
	Layout->AddChildToVerticalBox(StatusText);
	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseLabel"));
	CloseLabel->SetText(FText::FromString(TEXT("닫기")));
	CloseButton->AddChild(CloseLabel);
	Layout->AddChildToVerticalBox(CloseButton);
	CloseButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleCloseClicked);
}

void USWRoomMenuWidget::SetBusy(bool bBusy)
{
	if (SaveButton) SaveButton->SetIsEnabled(!bBusy);
	if (SaveAndExitButton) SaveAndExitButton->SetIsEnabled(!bBusy);
	if (StatusText) StatusText->SetText(FText::FromString(bBusy ? TEXT("저장 중...") : TEXT("")));
}

void USWRoomMenuWidget::SetResult(const FString& Message)
{
	if (SaveButton) SaveButton->SetIsEnabled(true);
	if (SaveAndExitButton) SaveAndExitButton->SetIsEnabled(true);
	if (StatusText) StatusText->SetText(FText::FromString(Message));
}

void USWRoomMenuWidget::HandleSaveClicked()
{
	if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwningPlayer())) Controller->RequestRoomSave();
}

void USWRoomMenuWidget::HandleSaveAndExitClicked()
{
	if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwningPlayer())) Controller->RequestRoomSaveAndExit();
}

void USWRoomMenuWidget::HandleCloseClicked()
{
	if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwningPlayer())) Controller->CloseRoomMenu();
}
