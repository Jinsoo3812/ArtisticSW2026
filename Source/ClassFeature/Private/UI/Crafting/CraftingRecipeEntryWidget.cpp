#include "UI/Crafting/CraftingRecipeEntryWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UCraftingRecipeEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RecipeButton)
	{
		RecipeButton->OnClicked.AddUniqueDynamic(this, &UCraftingRecipeEntryWidget::HandleRecipeButtonClicked);
	}

	RefreshVisuals();
}

void UCraftingRecipeEntryWidget::NativeDestruct()
{
	if (RecipeButton)
	{
		RecipeButton->OnClicked.RemoveDynamic(this, &UCraftingRecipeEntryWidget::HandleRecipeButtonClicked);
	}

	OnRecipeSelected.Unbind();
	Super::NativeDestruct();
}

void UCraftingRecipeEntryWidget::SetupFromEntry(const FCraftingListEntry& InEntry, bool bInSelected)
{
	EntryData = InEntry;
	bSelected = bInSelected;
	RefreshVisuals();
}

void UCraftingRecipeEntryWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;

	if (SelectionBorder)
	{
		SelectionBorder->SetBrushColor(bSelected ? SelectedBackgroundColor : UnselectedBackgroundColor);
	}
}

void UCraftingRecipeEntryWidget::HandleRecipeButtonClicked()
{
	if (!EntryData.RecipeId.IsNone())
	{
		OnRecipeSelected.ExecuteIfBound(EntryData.RecipeId);
	}
}

void UCraftingRecipeEntryWidget::RefreshVisuals()
{
	SetSelected(bSelected);

	if (DisplayNameText)
	{
		DisplayNameText->SetText(EntryData.DisplayName);
	}
}
