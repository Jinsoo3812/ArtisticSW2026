#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "SWConnectionLobbyWidget.generated.h"

class SEditableTextBox;
class STextBlock;
class SVerticalBox;

UCLASS()
class ARTISTICSW2026_API USWConnectionLobbyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
private:
	enum class EPanel : uint8 { Home, Create, Join };
	EPanel Panel = EPanel::Home;
	TSharedPtr<SVerticalBox> Content;
	TSharedPtr<SEditableTextBox> NameInput;
	TSharedPtr<SEditableTextBox> CodeInput;
	TSharedPtr<SEditableTextBox> PublicIPInput;
	TSharedPtr<STextBlock> StatusText;
	void RebuildContent();
	UFUNCTION() void HandleRoomChanged(ESWRoomState State, FText Message);
};
