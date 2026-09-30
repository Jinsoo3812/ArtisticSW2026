#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWDeathFlowWidget.generated.h"
UCLASS()
class CLASSFEATURE_API USWDeathFlowWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void UpdateFlow(bool bGameOver, bool bHost, bool bBusy, const FString& Waiting, const FString& Status);
 void FocusRetry();
protected:
 virtual void NativeOnInitialized() override;
 UFUNCTION() void RetryClicked();
 UPROPERTY() TObjectPtr<class UTextBlock> WaitingText;
 UPROPERTY() TObjectPtr<class UBorder> GameOverPanel;
 UPROPERTY() TObjectPtr<class UTextBlock> StatusText;
 UPROPERTY() TObjectPtr<class UButton> RetryButton;
};
