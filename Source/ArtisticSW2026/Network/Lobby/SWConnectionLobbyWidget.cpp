#include "Network/Lobby/SWConnectionLobbyWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"

USWRoomSubsystem* USWConnectionLobbyWidget::GetRoom() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr;
}

void USWConnectionLobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (CreateButton) CreateButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenCreate);
	if (ContinueButton) ContinueButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenContinue);
	if (JoinButton) JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenJoin);
	if (QuitButton) QuitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Quit);
	if (AutoHostButton) AutoHostButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitAuto);
	if (ManualHostButton) ManualHostButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitManual);
	if (JoinSubmitButton) JoinSubmitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitJoin);
	if (ConnectHostButton) ConnectHostButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConnectHost);
	if (ConfirmCreateButton) ConfirmCreateButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmCreate);
	if (ConfirmCancelButton) ConfirmCancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Back);
	if (BackButton) BackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Back);
	if (RoomCodeOutput) RoomCodeOutput->SetIsReadOnly(true);
}

void USWConnectionLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Hide legacy connectivity guidance authored in the lobby widget asset.
	if (WidgetTree) WidgetTree->ForEachWidget([](UWidget* Widget)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			const FString Value = Text->GetText().ToString();
			if (Value.Contains(TEXT("CGNAT"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("방화벽")) || Value.Contains(TEXT("firewall"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("NAT loopback"), ESearchCase::IgnoreCase))
			{
				Text->SetText(FText::GetEmpty());
				Text->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	});
	if (USWRoomSubsystem* Room = GetRoom()) Room->OnRoomChanged.AddUniqueDynamic(this, &ThisClass::HandleRoomChanged);
	Refresh();
}

void USWConnectionLobbyWidget::NativeDestruct()
{
	if (USWRoomSubsystem* Room = GetRoom()) Room->OnRoomChanged.RemoveDynamic(this, &ThisClass::HandleRoomChanged);
	Super::NativeDestruct();
}

void USWConnectionLobbyWidget::HandleRoomChanged(ESWRoomState State, FText Message) { Refresh(); }

void USWConnectionLobbyWidget::Refresh()
{
	USWRoomSubsystem* Room = GetRoom();
	const bool bCanHost = Room && Room->CanHost();
	const bool bHostForm = Panel == EPanel::Create || Panel == EPanel::Continue;
	const bool bCanSubmit = Room && (Room->GetRoomState() == ESWRoomState::Idle || Room->GetRoomState() == ESWRoomState::Failed);
	auto Show = [](UWidget* Widget, bool bShow)
	{
		if (Widget) Widget->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	};
	Show(HomePanel, Panel == EPanel::Home);
	Show(FormPanel, bHostForm || Panel == EPanel::Join);
	const ESlateVisibility ConfirmVisibility = Panel == EPanel::ConfirmCreate
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden;
	if (ConfirmPanel) ConfirmPanel->SetVisibility(ConfirmVisibility);
	if (ConfirmPanel1) ConfirmPanel1->SetVisibility(ConfirmVisibility);
	if (LobbySwitcher)
	{
		UWidget* Page = Panel == EPanel::Home ? HomePanel.Get()
			: Panel == EPanel::ConfirmCreate ? ConfirmPanel.Get() : FormPanel.Get();
		// HomePanel is nested inside SizeBox/Canvas; select the direct switcher child.
		while (Page && Page->GetParent() != LobbySwitcher) Page = Page->GetParent();
		if (Page) LobbySwitcher->SetActiveWidget(Page);
	}
	Show(NameSection, Panel != EPanel::Continue);
	Show(JoinSection, Panel == EPanel::Join);
	Show(HostSection, bHostForm);
	Show(ManualIPSection, bHostForm && Room && Room->NeedsManualPublicIP());
	Show(HostedRoomSection, bHostForm && Room && !Room->GetRoomCode().IsEmpty());
	Show(ContinueButton, bCanHost && Room->HasSavedRoom());
	if (CreateButton) CreateButton->SetIsEnabled(bCanHost);
	if (AutoHostButton) AutoHostButton->SetIsEnabled(bCanHost && bCanSubmit);
	if (ManualHostButton) ManualHostButton->SetIsEnabled(bCanHost && bCanSubmit);
	if (JoinSubmitButton) JoinSubmitButton->SetIsEnabled(bCanSubmit);
	if (ConnectHostButton) ConnectHostButton->SetIsEnabled(Room && Room->GetRoomState() == ESWRoomState::StartingServer && !Room->GetRoomCode().IsEmpty());
	if (RoomCodeOutput) RoomCodeOutput->SetText(FText::FromString(Room ? Room->GetRoomCode() : FString()));
	if (StatusText) StatusText->SetText(Room ? Room->GetRoomMessage() : FText::GetEmpty());
}

void USWConnectionLobbyWidget::OpenCreate()
{
	if (USWRoomSubsystem* Room = GetRoom(); Room && Room->CanHost()) { Panel = EPanel::Create; Refresh(); }
}
void USWConnectionLobbyWidget::OpenContinue()
{
	if (USWRoomSubsystem* Room = GetRoom(); Room && Room->CanHost() && Room->HasSavedRoom()) { Panel = EPanel::Continue; Refresh(); }
}
void USWConnectionLobbyWidget::OpenJoin() { Panel = EPanel::Join; Refresh(); }
void USWConnectionLobbyWidget::Back()
{
	if (USWRoomSubsystem* Room = GetRoom()) Room->CancelPendingOperation();
	Panel = EPanel::Home;
	PendingCreateName.Reset();
	PendingCreateIP.Reset();
	Refresh();
}

void USWConnectionLobbyWidget::SubmitHosting(const FString& PublicIP)
{
	USWRoomSubsystem* Room = GetRoom();
	if (!Room || !Room->CanHost()) return;
	if (Room->GetRoomState() != ESWRoomState::Idle && Room->GetRoomState() != ESWRoomState::Failed) return;
	if (Panel == EPanel::Continue) Room->ContinueRoom(FString(), PublicIP);
	else if (Panel == EPanel::Create && NameInput)
	{
		if (Room->HasSavedRoom())
		{
			PendingCreateName = NameInput->GetText().ToString();
			PendingCreateIP = PublicIP;
			Panel = EPanel::ConfirmCreate;
		}
		else Room->CreateRoom(NameInput->GetText().ToString(), PublicIP);
	}
	Refresh();
}
void USWConnectionLobbyWidget::SubmitAuto() { SubmitHosting(FString()); }
void USWConnectionLobbyWidget::SubmitManual() { if (PublicIPInput) SubmitHosting(PublicIPInput->GetText().ToString()); }
void USWConnectionLobbyWidget::ConfirmCreate()
{
	if (Panel != EPanel::ConfirmCreate) return;
	Panel = EPanel::Create;
	if (USWRoomSubsystem* Room = GetRoom()) Room->CreateRoom(PendingCreateName, PendingCreateIP);
	PendingCreateName.Reset();
	PendingCreateIP.Reset();
	Refresh();
}
void USWConnectionLobbyWidget::SubmitJoin()
{
	if (Panel != EPanel::Join || !NameInput || !CodeInput) return;
	if (USWRoomSubsystem* Room = GetRoom()) Room->JoinRoom(NameInput->GetText().ToString(), CodeInput->GetText().ToString());
}
void USWConnectionLobbyWidget::ConnectHost() { if (USWRoomSubsystem* Room = GetRoom()) Room->ConnectHostedRoom(); }
void USWConnectionLobbyWidget::Quit()
{
	if (USWRoomSubsystem* Room = GetRoom()) Room->CancelPendingOperation();
	UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, false);
}
