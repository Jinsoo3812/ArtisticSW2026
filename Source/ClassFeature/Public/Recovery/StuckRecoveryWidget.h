#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StuckRecoveryWidget.generated.h"

class UStuckRecoveryComponent;

/** Native guide works without a widget asset; Blueprint children can replace its layout. */
UCLASS()
class CLASSFEATURE_API UStuckRecoveryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetRecoverySource(UStuckRecoveryComponent* InRecovery, const FText& InKeyLabel);
	void ShowFailure();
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	FText GetGuideText() const;
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	float GetProgress() const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	UPROPERTY(BlueprintReadOnly, Category="Stuck Recovery")
	TObjectPtr<UStuckRecoveryComponent> Recovery;
	UPROPERTY(BlueprintReadOnly, Category="Stuck Recovery")
	FText KeyLabel;
private:
	double FailureUntil = 0.;
};
