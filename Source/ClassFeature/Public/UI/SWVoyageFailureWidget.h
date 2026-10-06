#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWVoyageFailureWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class CLASSFEATURE_API USWVoyageFailureWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Configure(bool bHost, const FString& Message, FSimpleDelegate InRetry, FSimpleDelegate InLeave);
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WaitingText;
	UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
	FSimpleDelegate RetryAction;
	FSimpleDelegate LeaveAction;
	UFUNCTION() void HandleRetry();
	UFUNCTION() void HandleLeave();
};
