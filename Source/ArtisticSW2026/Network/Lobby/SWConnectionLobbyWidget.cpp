#include "Network/Lobby/SWConnectionLobbyWidget.h"
#include "Network/SWConnectionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> USWConnectionLobbyWidget::RebuildWidget()
{
	SAssignNew(Content, SVerticalBox);
	RebuildContent();
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.025f, 0.04f, 0.07f))
		.Padding(36)
		[
			SNew(SScrollBox) + SScrollBox::Slot()
			[
				SNew(SBox).WidthOverride(560).HAlign(HAlign_Center)[Content.ToSharedRef()]
			]
		];
}

void USWConnectionLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (USWRoomSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomSubsystem>())
		Room->OnRoomChanged.AddDynamic(this, &USWConnectionLobbyWidget::HandleRoomChanged);
	RebuildContent();
}

void USWConnectionLobbyWidget::NativeDestruct()
{
	if (GetGameInstance())
		if (USWRoomSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomSubsystem>())
			Room->OnRoomChanged.RemoveDynamic(this, &USWConnectionLobbyWidget::HandleRoomChanged);
	Super::NativeDestruct();
}

void USWConnectionLobbyWidget::HandleRoomChanged(ESWRoomState State, FText Message)
{
	if (StatusText.IsValid()) StatusText->SetText(Message);
	RebuildContent();
}

void USWConnectionLobbyWidget::RebuildContent()
{
	if (!Content.IsValid()) return;
	const FString SavedName = NameInput.IsValid() ? NameInput->GetText().ToString() : FString();
	const FString SavedCode = CodeInput.IsValid() ? CodeInput->GetText().ToString() : FString();
	const FString SavedIP = PublicIPInput.IsValid() ? PublicIPInput->GetText().ToString() : FString();
	Content->ClearChildren();
	Content->AddSlot().AutoHeight().Padding(0, 15)[SNew(STextBlock).Text(FText::FromString(TEXT("ArtisticSW2026"))).ColorAndOpacity(FLinearColor::White)];
	USWRoomSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr;
	if (Panel == EPanel::Home)
	{
		Content->AddSlot().AutoHeight().Padding(0, 10)[SNew(SButton).IsEnabled(Room && Room->CanHost()).Text(FText::FromString(TEXT("방 만들기"))).OnClicked_Lambda([this]() { Panel = EPanel::Create; RebuildContent(); return FReply::Handled(); })];
		if (Room && Room->CanHost() && Room->HasSavedRoom())
			Content->AddSlot().AutoHeight().Padding(0, 10)[SNew(SButton).Text(FText::FromString(TEXT("이어하기"))).OnClicked_Lambda([this]() { Panel = EPanel::Continue; RebuildContent(); return FReply::Handled(); })];
		Content->AddSlot().AutoHeight().Padding(0, 10)[SNew(SButton).Text(FText::FromString(TEXT("방 들어가기"))).OnClicked_Lambda([this]() { Panel = EPanel::Join; RebuildContent(); return FReply::Handled(); })];
		Content->AddSlot().AutoHeight().Padding(0, 10)[SNew(SButton).Text(FText::FromString(TEXT("게임 종료"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) R->CancelPendingOperation(); UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, false); return FReply::Handled(); })];
	}
	else if (Panel == EPanel::ConfirmCreate)
	{
		Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("기존 방 정보와 진행 상황이 삭제됩니다. 새 방을 만드시겠습니까?"))).AutoWrapText(true)];
		Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(TEXT("새 방 만들기"))).OnClicked_Lambda([this]() { Panel = EPanel::Create; RebuildContent(); if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) R->CreateRoom(PendingCreateName, PendingCreateIP); return FReply::Handled(); })];
		Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(TEXT("취소"))).OnClicked_Lambda([this]() { Panel = EPanel::Home; RebuildContent(); return FReply::Handled(); })];
	}
	else
	{
		if (Panel != EPanel::Continue)
		{
			Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("이름")))];
			Content->AddSlot().AutoHeight().Padding(0, 8)[SAssignNew(NameInput, SEditableTextBox).Text(FText::FromString(SavedName))];
		}
		if (Panel == EPanel::Join)
		{
			Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("참가 코드")))];
			Content->AddSlot().AutoHeight().Padding(0, 8)[SAssignNew(CodeInput, SEditableTextBox).Text(FText::FromString(SavedCode))];
			Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(TEXT("입장"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) R->JoinRoom(NameInput->GetText().ToString(), CodeInput->GetText().ToString()); return FReply::Handled(); })];
		}
		else
		{
			Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(Room && Room->NeedsManualPublicIP() ? TEXT("공인 IP 자동 재시도") : Panel == EPanel::Continue ? TEXT("자동으로 이어하기") : TEXT("자동으로 방 만들기"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) { if (Panel == EPanel::Continue) R->ContinueRoom(FString(), FString()); else if (R->HasSavedRoom()) { PendingCreateName = NameInput->GetText().ToString(); PendingCreateIP.Empty(); Panel = EPanel::ConfirmCreate; RebuildContent(); } else R->CreateRoom(NameInput->GetText().ToString(), FString()); } return FReply::Handled(); })];
			if (Room && Room->NeedsManualPublicIP())
			{
				Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("자동 조회 실패 시에만 공인 IPv4 직접 입력")))];
				Content->AddSlot().AutoHeight().Padding(0, 8)[SAssignNew(PublicIPInput, SEditableTextBox).Text(FText::FromString(SavedIP))];
				Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(Panel == EPanel::Continue ? TEXT("입력한 IP로 이어하기") : TEXT("입력한 IP로 방 만들기"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) { if (Panel == EPanel::Continue) R->ContinueRoom(FString(), PublicIPInput->GetText().ToString()); else if (R->HasSavedRoom()) { PendingCreateName = NameInput->GetText().ToString(); PendingCreateIP = PublicIPInput->GetText().ToString(); Panel = EPanel::ConfirmCreate; RebuildContent(); } else R->CreateRoom(NameInput->GetText().ToString(), PublicIPInput->GetText().ToString()); } return FReply::Handled(); })];
			}
			if (Room && !Room->GetRoomCode().IsEmpty())
			{
				Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("참가 코드 (선택해 복사)")))];
				Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SEditableTextBox).Text(FText::FromString(Room->GetRoomCode())).IsReadOnly(true)];
				Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(TEXT("서버 접속"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) R->ConnectHostedRoom(); return FReply::Handled(); })];
			}
		}
		Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton).Text(FText::FromString(TEXT("취소 / 뒤로"))).OnClicked_Lambda([this]() { if (USWRoomSubsystem* R = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) R->CancelPendingOperation(); Panel = EPanel::Home; RebuildContent(); return FReply::Handled(); })];
	}
	Content->AddSlot().AutoHeight().Padding(0, 12)[SAssignNew(StatusText, STextBlock).Text(Room ? Room->GetRoomMessage() : FText::GetEmpty()).ColorAndOpacity(FLinearColor::White)];
	Content->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("호스트: 공유기 UDP 7777 포트 전달과 Windows 방화벽 허용이 필요합니다. CGNAT에서는 연결할 수 없습니다."))).AutoWrapText(true)];
}
