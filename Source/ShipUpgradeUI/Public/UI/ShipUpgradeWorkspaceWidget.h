#pragma once

#include "CoreMinimal.h"
#include "UI/FacilityHubWidget.h"
#include "ShipUpgradeWorkspaceWidget.generated.h"

class UTextBlock;

UENUM(BlueprintType)
enum class EShipUpgradeWorkspaceMenuSelection : uint8
{
	None,
	ShipUpgrade,
	ItemCrafting,
	SkillUpgrade
};

/**
 * Compatibility parent for the existing WBP_WorkspaceScreen design.
 * All top-level behavior now comes from the common FacilityHub shell.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeWorkspaceWidget : public UFacilityHubWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual void NativeOnFacilityTabChanged(int32 NewTabIndex) override;

	/** Background tint for the selected top-level button in WBP_WorkspaceScreen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace|Style")
	FLinearColor SelectedTopMenuBackgroundColor = FLinearColor(0.0f, 1.0f, 1.0f, 1.0f);

	/** White keeps the unselected buttons' designer-authored brush colors unchanged. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace|Style")
	FLinearColor UnselectedTopMenuBackgroundColor = FLinearColor::White;

	/** Persistent Selected state. It remains active until another top-level menu is selected. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Ship Upgrade|Workspace|State")
	EShipUpgradeWorkspaceMenuSelection SelectedTopMenu = EShipUpgradeWorkspaceMenuSelection::None;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_RightTitle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText ShipUpgradeTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "ShipUpgradeTabTitle", "배 강화");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText ItemCraftingTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "ItemCraftingTabTitle", "아이템 제작");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Workspace")
	FText SkillUpgradeTabTitle = NSLOCTEXT("ShipUpgradeWorkspace", "SkillUpgradeTabTitle", "스킬 강화");

private:
	UFUNCTION()
	void HandleShipUpgradeMenuClicked();

	UFUNCTION()
	void HandleItemCraftingMenuClicked();

	UFUNCTION()
	void HandleSkillUpgradeMenuClicked();

	void SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection NewSelection);
	void ApplySelectedTopMenuColor();
};
