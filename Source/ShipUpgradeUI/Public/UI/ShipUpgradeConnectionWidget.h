#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipUpgradeConnectionWidget.generated.h"

class UImage;
class UTexture2D;

/** Native geometry/state for one graph connection. */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeConnectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureConnection(bool bInDashed);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Line;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	TObjectPtr<UTexture2D> SolidLineTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	TObjectPtr<UTexture2D> DashedLineTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor SolidLineColor = FLinearColor(0.0f, 0.75f, 0.8f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor DashedLineColor = FLinearColor(0.3f, 0.45f, 0.5f, 0.8f);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnConnectionStyleApplied(bool bDashed);
};
