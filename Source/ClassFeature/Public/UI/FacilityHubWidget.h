#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FacilityHubWidget.generated.h"

class AActor;
class UCraftingComponent;
class UCraftingPanelWidget;
class UButton;
class UWidgetSwitcher;

/**
 * The single top-level shell for every facility screen.
 * C++ owns context, tab lifecycle, and input-safe closing; WBP owns layout and styling.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API UFacilityHubWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void InitializeForContext(AActor* InContextActor);

	/** Request server approval for entering the crafting tab. Returns false if local references are invalid. */
	UFUNCTION(BlueprintCallable, Category = "Facility Hub|Crafting")
	bool RequestOpenCraftingTab();

	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void ShowShipUpgradeTab();

	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void ShowItemCraftingTab();

	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void ShowSkillUpgradeTab();

	/** Close the hub and restore game input through the owning player controller. */
	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void RequestCloseFacilityHub();

	UFUNCTION(BlueprintPure, Category = "Facility Hub|Crafting")
	bool IsCraftingTabActive() const;

	/** Called by the owning controller before removing the widget. */
	void PrepareToClose();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Facility Hub", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<AActor> ContextActor;

	UPROPERTY(BlueprintReadOnly, Category = "Facility Hub")
	TObjectPtr<UCraftingComponent> CraftingComponent;

	/** Existing teammate panel. It may be placed in WBP or injected into tab 1 at runtime. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCraftingPanelWidget> CraftingPanelWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Facility Hub|Crafting")
	TSubclassOf<UCraftingPanelWidget> CraftingPanelWidgetClass;

	/** Names used by the ship-upgrade workspace design. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_Content;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ShipUpgrade;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ItemCrafting;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_SkillUpgrade;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Close;

	/** Names used by the teammate's original facility WBP. */
	UPROPERTY(BlueprintReadOnly, Category = "Facility Hub|Navigation", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> MainWidgetSwitcher;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CraftingTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Facility Hub|Navigation")
	int32 ShipUpgradeTabIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Facility Hub|Navigation")
	int32 ItemCraftingTabIndex = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Facility Hub|Navigation")
	int32 SkillUpgradeTabIndex = 2;

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility Hub|Style")
	void BP_OnFacilityTabChanged(int32 NewTabIndex);

	/** Native extension point for specialized workspace shells. */
	virtual void NativeOnFacilityTabChanged(int32 NewTabIndex);

	/** Implement in WBP_FacilityHub to switch to the crafting panel after server approval. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility Hub|Crafting", meta = (DisplayName = "On Crafting Tab Approved"))
	void BP_OnCraftingTabApproved(AActor* ApprovedContext);

	/** Implement in WBP_FacilityHub to reset its crafting panel state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility Hub|Crafting", meta = (DisplayName = "On Crafting Tab Closed"))
	void BP_OnCraftingTabClosed();

private:
	void ResolveCraftingComponent();
	void EnsureCraftingPanel();
	void BindCraftingEvents();
	void UnbindCraftingEvents();
	void BindNavigation();
	void UnbindNavigation();
	void CloseCraftingTabIfActive();
	void ShowTab(int32 TabIndex);
	UWidgetSwitcher* GetTabSwitcher() const;

	UFUNCTION()
	void HandleCraftingScreenOpened(AActor* ApprovedContext);

	UFUNCTION()
	void HandleCraftingScreenClosed();
};
