#include "UI/SWRoomMenuWidget.h"

#include "BasePlayerController.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void USWRoomMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	const USWRoomSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr;
	const bool bInRoom = Room && Room->GetRoomState() == ESWRoomState::Playing;
	RoomCode = bInRoom ? Room->GetRoomCode() : FString();
	const FString PlayerName = bInRoom ? Room->GetDisplayName() : TEXT("정보 없음");
	if (PlayerNameText) PlayerNameText->SetText(FText::FromString(FString::Printf(TEXT("내 이름: %s"), *PlayerName)));
	if (RoomCodeText) RoomCodeText->SetText(FText::FromString(FString::Printf(TEXT("방 코드: %s"), RoomCode.IsEmpty() ? TEXT("없음") : *RoomCode)));
	if (CopyRoomCodeButton)
	{
		CopyRoomCodeButton->SetIsEnabled(!RoomCode.IsEmpty());
		CopyRoomCodeButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleCopyRoomCodeClicked);
	}
	if (SaveButton) SaveButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleSaveClicked);
	if (SaveAndExitButton) SaveAndExitButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleSaveAndExitClicked);
	if (CloseButton) CloseButton->OnClicked.AddDynamic(this, &USWRoomMenuWidget::HandleCloseClicked);
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
