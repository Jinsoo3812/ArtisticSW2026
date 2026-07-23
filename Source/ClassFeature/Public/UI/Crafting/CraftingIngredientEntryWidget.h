#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/CraftingTypes.h"
#include "CraftingIngredientEntryWidget.generated.h"

class UImage;
class UTextBlock;

/** Displays one ingredient for a single craft in the selected recipe details. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingIngredientEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromIngredient(const FCraftingIngredientView& InIngredient);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IngredientIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> IngredientNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> OwnedQuantityText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RequiredQuantityText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor EnoughQuantityColor = FLinearColor(0.25f, 0.9f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Style")
	FLinearColor MissingQuantityColor = FLinearColor(0.95f, 0.3f, 0.2f, 1.0f);
};
