#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "CraftingMenuEntryWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DELEGATE_OneParam(FOnCraftingMenuRecipeActivated, FName);

/** One category heading or selectable recipe row in WorkTableScreen's left menu. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingMenuEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupCategory(const FText& InLabel, int32 InDepth);
	void SetupRecipe(FName InRecipeId, const FText& InLabel, int32 InDepth, bool bInSelected);
	void SetSelected(bool bInSelected);

	FOnCraftingMenuRecipeActivated OnRecipeActivated;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Styled in WBP_CraftingMenuEntry. Native construction is only a fallback. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Entry;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Entry;

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
	void BuildWidgetTree();
	void RefreshVisuals();

	UFUNCTION()
	void HandleClicked();

	FName RecipeId;
	FText Label;
	int32 Depth = 0;
	bool bCategory = false;
	bool bSelected = false;
	bool bDefaultButtonStyleCached = false;
	FButtonStyle DefaultButtonStyle;
};
