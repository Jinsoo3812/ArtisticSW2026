#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeMaterialRowWidget.generated.h"

struct FStreamableHandle;
class UImage;
class UTextBlock;

UCLASS(Abstract, BlueprintType)
class CLASSFEATURE_API UShipUpgradeMaterialRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeDestruct() override;
	void ApplyMaterialView(const FShipUpgradeMaterialView& InView);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_ItemIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ItemName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor EnoughColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor InsufficientColor = FLinearColor(1.0f, 0.25f, 0.15f, 1.0f);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnMaterialStateApplied(bool bEnough);

private:
	void RequestIconLoad();

	UPROPERTY(Transient)
	FShipUpgradeMaterialView MaterialView;

	FSoftObjectPath PendingIconPath;
	TSharedPtr<FStreamableHandle> IconLoadHandle;
};
