#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipUpgradeWorkspaceWidget.generated.h"

class UButton;
class UWidgetSwitcher;

/**
 * Native navigation shell for the integrated workbench.
 * Button and switcher names are the only required Blueprint connection.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeWorkspaceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "Workspace")
	void ShowShipUpgradeTab();

	UFUNCTION(BlueprintCallable, Category = "Workspace")
	void ShowItemCraftingTab();

	UFUNCTION(BlueprintCallable, Category = "Workspace")
	void ShowSkillUpgradeTab();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_Content;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ShipUpgrade;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ItemCrafting;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_SkillUpgrade;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Close;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Workspace")
	int32 ShipUpgradeTabIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Workspace")
	int32 ItemCraftingTabIndex = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Workspace")
	int32 SkillUpgradeTabIndex = 2;

	UFUNCTION(BlueprintImplementableEvent, Category = "Workspace|Style")
	void BP_OnWorkspaceTabChanged(int32 NewTabIndex);

private:
	UFUNCTION()
	void HandleCloseClicked();

	void ShowTab(int32 TabIndex);
};
