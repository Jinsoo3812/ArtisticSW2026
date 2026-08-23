#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "CraftingMenuEntryWidget.generated.h"

class UButton;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

DECLARE_DELEGATE_OneParam(FOnCraftingMenuRecipeActivated, FName);
DECLARE_DELEGATE_OneParam(FOnCraftingMenuCategoryActivated, FString);

/** One category heading or selectable recipe row in WorkTableScreen's left menu. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingMenuEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupCategory(const FString& InCategoryPath, const FText& InLabel, int32 InDepth);
	void SetupRecipe(FName InRecipeId, const FText& InLabel, int32 InDepth, bool bInSelected);
	void SetSelected(bool bInSelected);
	FName GetRecipeId() const { return RecipeId; }

	FOnCraftingMenuRecipeActivated OnRecipeActivated;
	FOnCraftingMenuCategoryActivated OnCategoryActivated;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Layout and styling are authored in WBP_CraftingMenuEntry. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> Button_Entry;

	/** Designer pages: 0 top category, 1 nested category, 2 recipe. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_EntryLayout;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_CategoryTop;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_CategoryNested;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Recipe;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox_SelectionUnderline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor CategoryTextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor ItemTextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor SelectedTextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor SelectedHighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.28f);

#if WITH_EDITOR
public:
	void SetItemTextColor(FLinearColor InColor) { ItemTextColor = InColor; }
#endif

private:
	void RefreshVisuals();

	UFUNCTION()
	void HandleClicked();

	FName RecipeId;
	FString CategoryPath;
	FText Label;
	int32 Depth = 0;
	bool bCategory = false;
	bool bSelected = false;
	bool bDefaultButtonStyleCached = false;
	FButtonStyle DefaultButtonStyle;
};
