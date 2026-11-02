#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWRoomMenuWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class CLASSFEATURE_API USWRoomMenuWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	void SetBusy(bool bBusy);
	void SetResult(const FString& Message);

private:
	UFUNCTION() void HandleSaveClicked();
	UFUNCTION() void HandleSaveAndExitClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleCopyRoomCodeClicked();
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PlayerNameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RoomCodeText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CopyRoomCodeButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SaveButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SaveAndExitButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;
	FString RoomCode;
};
