#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FacilityHubWidget.generated.h"

class AActor;
class UCraftingComponent;
class UCraftingPanelWidget;

/**
 * Blueprint-facing API for the integrated facility UI.
 * Widget hierarchy, styling, and screen switching are owned entirely by WBP_FacilityHub.
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

	/** Designer-authored WBP_CraftingPanel placed inside the existing CraftingPanel container. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCraftingPanelWidget> CraftingPanelWidget;

	/** Implement in WBP_FacilityHub to switch to the crafting panel after server approval. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility Hub|Crafting", meta = (DisplayName = "On Crafting Tab Approved"))
	void BP_OnCraftingTabApproved(AActor* ApprovedContext);

	/** Implement in WBP_FacilityHub to reset its crafting panel state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility Hub|Crafting", meta = (DisplayName = "On Crafting Tab Closed"))
	void BP_OnCraftingTabClosed();

private:
	void ResolveCraftingComponent();
	void BindCraftingEvents();
	void UnbindCraftingEvents();

	UFUNCTION()
	void HandleCraftingScreenOpened(AActor* ApprovedContext);

	UFUNCTION()
	void HandleCraftingScreenClosed();
};
