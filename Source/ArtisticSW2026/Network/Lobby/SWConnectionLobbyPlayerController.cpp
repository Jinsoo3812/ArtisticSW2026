#include "Network/Lobby/SWConnectionLobbyPlayerController.h"
#include "Network/Lobby/SWConnectionLobbyWidget.h"
#include "Network/SWNetworkLog.h"
#include "UObject/SoftObjectPath.h"

void ASWConnectionLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalPlayerController()) return;
	const FSoftClassPath WidgetPath(TEXT("/Game/Blueprints/02_UI/UI_Lobby/WBP_ConnectionLobby.WBP_ConnectionLobby_C"));
	UClass* WidgetClass = WidgetPath.TryLoadClass<USWConnectionLobbyWidget>();
	if (!WidgetClass)
	{
		UE_LOG(LogSWConnection, Error, TEXT("WBP_ConnectionLobby is missing or invalid. The lobby requires its Designer layout."));
		return;
	}
	LobbyWidget = CreateWidget<USWConnectionLobbyWidget>(this, WidgetClass);
	if (!LobbyWidget)
	{
		UE_LOG(LogSWConnection, Error, TEXT("Lobby widget creation failed."));
		return;
	}
	LobbyWidget->AddToViewport();
	FInputModeUIOnly Input;
	Input.SetWidgetToFocus(LobbyWidget->TakeWidget());
	SetInputMode(Input);
	bShowMouseCursor = true;
}

void ASWConnectionLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LobbyWidget) LobbyWidget->RemoveFromParent();
	LobbyWidget = nullptr;
	bShowMouseCursor = false;
	Super::EndPlay(EndPlayReason);
}
