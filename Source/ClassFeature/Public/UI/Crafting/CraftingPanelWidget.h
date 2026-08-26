#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/CraftingTypes.h"
#include "CraftingPanelWidget.generated.h"

class UCraftingCompleteWidget;
class UCraftingComponent;
class UCraftingIngredientEntryWidget;
class UButton;
class UImage;
class UPanelWidget;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCraftingRecipeSelected, FName, RecipeId);

/** Displays only the recipe selected from WorkTableScreen's left crafting menu. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ActivateCraftingPanel(UCraftingComponent* InCraftingComponent);
	void DeactivateCraftingPanel();

	UFUNCTION(BlueprintCallable, Category = "Crafting|UI")
	bool SelectRecipe(FName RecipeId);

	UFUNCTION(BlueprintCallable, Category = "Crafting|UI")
	void RefreshSelectedRecipe();

	UFUNCTION(BlueprintPure, Category = "Crafting|UI")
	FName GetSelectedRecipeId() const { return SelectedRecipeId; }

#if WITH_EDITOR
	void SetIngredientEntryClass(TSubclassOf<UCraftingIngredientEntryWidget> InClass)
	{
		IngredientEntryClass = InClass;
	}
	TSubclassOf<UCraftingIngredientEntryWidget> GetIngredientEntryClass() const
	{
		return IngredientEntryClass;
	}
#endif

	UPROPERTY(BlueprintAssignable, Category = "Crafting|UI")
	FOnCraftingRecipeSelected OnRecipeSelected;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** index 0: selected recipe, index 1: WBP_CraftingComplete. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_CraftingState;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> RecipeDetailPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyDetailText;

	/** Central result icon. WBP places BackgroundBlur_ResultIcon directly above it. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ResultIconImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultQuantityText;

	/** Fixed directional ingredient hosts. Recipe data is limited to four ingredients. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientNorthSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientEastSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientSouthSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientWestSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MissingRecipeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CraftButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CraftButtonText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CraftResultText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCraftingCompleteWidget> CraftingCompleteWidget;

	UPROPERTY(EditDefaultsOnly, Category = "Crafting|UI")
	TSubclassOf<UCraftingIngredientEntryWidget> IngredientEntryClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor EnabledCraftTextColor = FLinearColor(0.95f, 0.82f, 0.35f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor DisabledCraftTextColor = FLinearColor(0.35f, 0.36f, 0.38f, 1.0f);

private:
	void BindCraftingEvents();
	void UnbindCraftingEvents();
	void ClearRecipeDetails();
	bool RefreshSelectedRecipeDetails();
	void ApplyRecipeDetails(const FCraftingDetailsView& Details);
	void SetCraftButtonAvailable(bool bAvailable);
	static FText GetCraftResultText(ECraftingFailureReason FailureReason);

	UFUNCTION()
	void HandleCraftingDataChanged();

	UFUNCTION()
	void HandleCraftButtonClicked();

	UFUNCTION()
	void HandleCraftingResult(const FCraftingResult& Result);

	void HandleCraftingCompleteDismissed();

	UPROPERTY(Transient)
	TObjectPtr<UCraftingComponent> CraftingComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCraftingIngredientEntryWidget>> SpawnedIngredientEntries;

	FCraftingListEntry SelectedRecipeHeader;
	FName SelectedRecipeId;
	FGuid PendingRequestId;
	bool bPanelActive = false;
	bool bCraftRequestPending = false;
};
