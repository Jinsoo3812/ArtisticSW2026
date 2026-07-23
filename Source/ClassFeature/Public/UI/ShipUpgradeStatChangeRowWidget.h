#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeStatChangeRowWidget.generated.h"

class UTextBlock;

UCLASS(Abstract, BlueprintType)
class CLASSFEATURE_API UShipUpgradeStatChangeRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ApplyStatChange(const FShipStatChangeView& InChange);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Change;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor ImprovementColor = FLinearColor(0.1f, 1.0f, 0.55f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor WorseningColor = FLinearColor(1.0f, 0.25f, 0.15f, 1.0f);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnStatChangeApplied(bool bImprovesStat);
};
