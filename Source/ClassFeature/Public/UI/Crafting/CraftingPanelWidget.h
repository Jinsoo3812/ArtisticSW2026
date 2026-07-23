#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CraftingPanelWidget.generated.h"

class UCraftingComponent;
class UCraftingIngredientEntryWidget;
class UCraftingRecipeEntryWidget;
class UImage;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UWidget;
struct FCraftingDetailsView;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCraftingRecipeSelected, FName, RecipeId);

/** Owns the recipe list state. All fixed child widgets are authored in WBP_CraftingPanel. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ActivateCraftingPanel(UCraftingComponent* InCraftingComponent);
	void DeactivateCraftingPanel();

	UFUNCTION(BlueprintCallable, Category = "Crafting|UI")
	void RefreshRecipeList();

	UFUNCTION(BlueprintPure, Category = "Crafting|UI")
	FName GetSelectedRecipeId() const { return SelectedRecipeId; }

	UPROPERTY(BlueprintAssignable, Category = "Crafting|UI")
	FOnCraftingRecipeSelected OnRecipeSelected;

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> RecipeScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyRecipeText;

	/** Right-hand details container. Hidden until the player selects a recipe. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> RecipeDetailPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyDetailText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ResultIconImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultQuantityText;

	/** Designer-authored VerticalBox or another panel that receives ingredient entries. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientList;

	/** Shown instead of IngredientList while a required recipe item is missing. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MissingRecipeText;

	UPROPERTY(EditDefaultsOnly, Category = "Crafting|UI")
	TSubclassOf<UCraftingRecipeEntryWidget> RecipeEntryClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crafting|UI")
	TSubclassOf<UCraftingIngredientEntryWidget> IngredientEntryClass;

private:
	void BindCraftingEvents();
	void UnbindCraftingEvents();
	void ClearRecipeList();
	void ClearRecipeDetails();
	bool RefreshSelectedRecipeDetails();
	void ApplyRecipeDetails(const FCraftingDetailsView& Details);
	void UpdateRecipeEntrySelection();
	void HandleRecipeSelected(FName RecipeId);

	UFUNCTION()
	void HandleCraftingDataChanged();

	UPROPERTY(Transient)
	TObjectPtr<UCraftingComponent> CraftingComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCraftingRecipeEntryWidget>> SpawnedRecipeEntries;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCraftingIngredientEntryWidget>> SpawnedIngredientEntries;

	FName SelectedRecipeId;
	bool bPanelActive = false;
};
