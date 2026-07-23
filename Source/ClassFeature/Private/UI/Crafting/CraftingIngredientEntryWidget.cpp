#include "UI/Crafting/CraftingIngredientEntryWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCraftingIngredientEntryWidget::SetupFromIngredient(const FCraftingIngredientView& InIngredient)
{
	if (IngredientNameText)
	{
		IngredientNameText->SetText(InIngredient.DisplayName);
	}

	if (OwnedQuantityText)
	{
		OwnedQuantityText->SetText(FText::AsNumber(InIngredient.OwnedQuantity));
		OwnedQuantityText->SetColorAndOpacity(FSlateColor(
			InIngredient.bEnough ? EnoughQuantityColor : MissingQuantityColor));
	}

	if (RequiredQuantityText)
	{
		RequiredQuantityText->SetText(FText::AsNumber(InIngredient.RequiredQuantity));
	}

	if (IngredientIconImage)
	{
		UTexture2D* Icon = InIngredient.Icon.LoadSynchronous();
		IngredientIconImage->SetBrushFromTexture(Icon, true);
		IngredientIconImage->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}
