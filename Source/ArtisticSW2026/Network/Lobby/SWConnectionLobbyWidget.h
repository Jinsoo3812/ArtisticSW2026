#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "SWConnectionLobbyWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UWidgetSwitcher;

/** Designer owns layout and labels; native code handles actions and visibility. */
UCLASS()
class ARTISTICSW2026_API USWConnectionLobbyWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Lobby") void OpenCreate();
	UFUNCTION(BlueprintCallable, Category="Lobby") void OpenContinue();
	UFUNCTION(BlueprintCallable, Category="Lobby") void OpenJoin();
	UFUNCTION(BlueprintCallable, Category="Lobby") void Back();
	UFUNCTION(BlueprintCallable, Category="Lobby") void SubmitAuto();
	UFUNCTION(BlueprintCallable, Category="Lobby") void SubmitManual();
	UFUNCTION(BlueprintCallable, Category="Lobby") void ConfirmCreate();
	UFUNCTION(BlueprintCallable, Category="Lobby") void SubmitJoin();
	UFUNCTION(BlueprintCallable, Category="Lobby") void ConnectHost();
	UFUNCTION(BlueprintCallable, Category="Lobby") void Quit();
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// Optional bindings allow custom layouts to compile while migrating.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> LobbySwitcher;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> HomePanel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> FormPanel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> ConfirmPanel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> ConfirmPanel1;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> NameSection;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> JoinSection;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> HostSection;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> ManualIPSection;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> HostedRoomSection;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CreateButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ContinueButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> JoinButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> QuitButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> AutoHostButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ManualHostButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> JoinSubmitButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConnectHostButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConfirmCreateButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConfirmCancelButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> BackButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UEditableTextBox> NameInput;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UEditableTextBox> CodeInput;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UEditableTextBox> PublicIPInput;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UEditableTextBox> RoomCodeOutput;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;
private:
	enum class EPanel : uint8 { Home, Create, ConfirmCreate, Continue, Join };
	EPanel Panel = EPanel::Home;
	FString PendingCreateName;
	FString PendingCreateIP;
	USWRoomSubsystem* GetRoom() const;
	void Refresh();
	void SubmitHosting(const FString& PublicIP);
	UFUNCTION() void HandleRoomChanged(ESWRoomState State, FText Message);
};
