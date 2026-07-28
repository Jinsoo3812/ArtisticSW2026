#pragma once

#include "CoreMinimal.h"
#include "UI/FacilityHubWidget.h"
#include "ShipUpgradeWorkspaceWidget.generated.h"

class UTextBlock;

/**
 * Compatibility parent for the existing WBP_WorkspaceScreen design.
 * All top-level behavior now comes from the common FacilityHub shell.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeWorkspaceWidget : public UFacilityHubWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnFacilityTabChanged(int32 NewTabIndex) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_RightTitle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText ShipUpgradeTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "ShipUpgradeTabTitle", "배 강화");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText ItemCraftingTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "ItemCraftingTabTitle", "아이템 제작");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText SkillUpgradeTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "SkillUpgradeTabTitle", "스킬 강화");
};
