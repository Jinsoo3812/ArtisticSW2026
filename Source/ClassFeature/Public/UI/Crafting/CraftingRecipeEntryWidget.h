#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/CraftingTypes.h"
#include "CraftingRecipeEntryWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;

DECLARE_DELEGATE_OneParam(FCraftingRecipeEntrySelected, FName);

/** Displays one selectable recipe name in the left-hand recipe list. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingRecipeEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromEntry(const FCraftingListEntry& InEntry, bool bInSelected);
	void SetSelected(bool bInSelected);

	FName GetRecipeId() const { return EntryData.RecipeId; }

	FCraftingRecipeEntrySelected OnRecipeSelected;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RecipeButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> SelectionBorder;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DisplayNameText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor SelectedBackgroundColor = FLinearColor(0.08f, 0.45f, 0.55f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor UnselectedBackgroundColor = FLinearColor(0.03f, 0.04f, 0.05f, 0.85f);

private:
	UFUNCTION()
	void HandleRecipeButtonClicked();

	void RefreshVisuals();

	UPROPERTY(Transient)
	FCraftingListEntry EntryData;

	bool bSelected = false;
};
