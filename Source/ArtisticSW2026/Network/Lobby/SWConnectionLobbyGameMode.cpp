#include "Network/Lobby/SWConnectionLobbyGameMode.h"
#include "Network/Lobby/SWConnectionLobbyPlayerController.h"

ASWConnectionLobbyGameMode::ASWConnectionLobbyGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
	PlayerControllerClass = ASWConnectionLobbyPlayerController::StaticClass();
}
