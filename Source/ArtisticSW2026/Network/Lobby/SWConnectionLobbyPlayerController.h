#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SWConnectionLobbyPlayerController.generated.h"

class USWConnectionLobbyWidget;

UCLASS()
class ARTISTICSW2026_API ASWConnectionLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY() TObjectPtr<USWConnectionLobbyWidget> LobbyWidget;
};
