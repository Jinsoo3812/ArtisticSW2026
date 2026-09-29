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
	UPROPERTY(Transient) TObjectPtr<UButton> SaveButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SaveAndExitButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	FString RoomCode;
};
