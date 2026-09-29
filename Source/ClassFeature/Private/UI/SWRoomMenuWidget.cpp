#include "UI/SWRoomMenuWidget.h"

#include "BasePlayerController.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
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
	const USWRoomSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr;
	const bool bInRoom = Room && Room->GetRoomState() == ESWRoomState::Playing;
	RoomCode = bInRoom ? Room->GetRoomCode() : FString();
	const FString PlayerName = bInRoom ? Room->GetDisplayName() : TEXT("정보 없음");
	UTextBlock* NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlayerName"));
	NameText->SetText(FText::FromString(FString::Printf(TEXT("내 이름: %s"), *PlayerName)));
	Layout->AddChildToVerticalBox(NameText);
	UHorizontalBox* CodeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomCodeRow"));
	UTextBlock* CodeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RoomCode"));
	CodeText->SetText(FText::FromString(FString::Printf(TEXT("방 코드: %s"), RoomCode.IsEmpty() ? TEXT("없음") : *RoomCode)));
	CodeRow->AddChildToHorizontalBox(CodeText);
	UButton* CopyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CopyRoomCodeButton"));
	UTextBlock* CopyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CopyRoomCodeLabel"));
	CopyLabel->SetText(FText::FromString(TEXT("복사")));
	CopyButton->AddChild(CopyLabel);
	CopyButton->SetIsEnabled(!RoomCode.IsEmpty());
	CopyButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleCopyRoomCodeClicked);
	CodeRow->AddChildToHorizontalBox(CopyButton);
	Layout->AddChildToVerticalBox(CodeRow);
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

void USWRoomMenuWidget::HandleCopyRoomCodeClicked()
{
	if (RoomCode.IsEmpty()) return;
	FPlatformApplicationMisc::ClipboardCopy(*RoomCode);
	if (StatusText) StatusText->SetText(FText::FromString(TEXT("방 코드를 복사했습니다.")));
}
