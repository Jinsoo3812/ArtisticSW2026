#include "UI/Crafting/CraftingPanelWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Crafting/CraftingComponent.h"
#include "Crafting/CraftingTypes.h"
#include "Engine/Texture2D.h"
#include "UI/Crafting/CraftingIngredientEntryWidget.h"
#include "UI/Crafting/CraftingRecipeEntryWidget.h"

void UCraftingPanelWidget::NativeDestruct()
{
	DeactivateCraftingPanel();
	Super::NativeDestruct();
}

void UCraftingPanelWidget::ActivateCraftingPanel(UCraftingComponent* InCraftingComponent)
{
	if (CraftingComponent != InCraftingComponent)
	{
		UnbindCraftingEvents();
		CraftingComponent = InCraftingComponent;
	}

	bPanelActive = CraftingComponent != nullptr;
	BindCraftingEvents();
	RefreshRecipeList();
}

void UCraftingPanelWidget::DeactivateCraftingPanel()
{
	bPanelActive = false;
	UnbindCraftingEvents();
	CraftingComponent = nullptr;
	SelectedRecipeId = NAME_None;
	ClearRecipeList();
	ClearRecipeDetails();
}

void UCraftingPanelWidget::BindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.AddUniqueDynamic(this, &UCraftingPanelWidget::HandleCraftingDataChanged);
	}
}

void UCraftingPanelWidget::UnbindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.RemoveDynamic(this, &UCraftingPanelWidget::HandleCraftingDataChanged);
	}
}

void UCraftingPanelWidget::RefreshRecipeList()
{
	if (!RecipeScrollBox)
	{
		SelectedRecipeId = NAME_None;
		ClearRecipeDetails();
		return;
	}

	const FName PreviousSelection = SelectedRecipeId;
	ClearRecipeList();

	if (!bPanelActive || !CraftingComponent || !RecipeEntryClass)
	{
		SelectedRecipeId = NAME_None;
		ClearRecipeDetails();
		if (EmptyRecipeText)
		{
			EmptyRecipeText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	FCraftingListQuery Query;
	Query.SearchText = FString();
	Query.bIncludeLocked = true;
	Query.bIncludeDisabled = false;

	const TArray<FCraftingListEntry> RecipeEntries = CraftingComponent->GetCraftableList(Query);
	bool bPreviousSelectionStillExists = false;

	for (const FCraftingListEntry& RecipeEntry : RecipeEntries)
	{
		UCraftingRecipeEntryWidget* EntryWidget = CreateWidget<UCraftingRecipeEntryWidget>(this, RecipeEntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		const bool bSelected = !PreviousSelection.IsNone() && RecipeEntry.RecipeId == PreviousSelection;
		bPreviousSelectionStillExists |= bSelected;
		EntryWidget->SetupFromEntry(RecipeEntry, bSelected);
		EntryWidget->OnRecipeSelected.BindUObject(this, &UCraftingPanelWidget::HandleRecipeSelected);

		RecipeScrollBox->AddChild(EntryWidget);
		SpawnedRecipeEntries.Add(EntryWidget);
	}

	SelectedRecipeId = bPreviousSelectionStillExists ? PreviousSelection : NAME_None;
	if (SelectedRecipeId.IsNone())
	{
		ClearRecipeDetails();
	}
	else
	{
		RefreshSelectedRecipeDetails();
	}

	if (EmptyRecipeText)
	{
		EmptyRecipeText->SetVisibility(
			SpawnedRecipeEntries.IsEmpty()
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}
}

void UCraftingPanelWidget::ClearRecipeList()
{
	for (UCraftingRecipeEntryWidget* EntryWidget : SpawnedRecipeEntries)
	{
		if (EntryWidget)
		{
			EntryWidget->OnRecipeSelected.Unbind();
		}
	}
	SpawnedRecipeEntries.Reset();

	if (RecipeScrollBox)
	{
		RecipeScrollBox->ClearChildren();
	}
}

void UCraftingPanelWidget::ClearRecipeDetails()
{
	SpawnedIngredientEntries.Reset();
	if (IngredientList)
	{
		IngredientList->ClearChildren();
		IngredientList->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (ResultIconImage)
	{
		ResultIconImage->SetBrushFromTexture(nullptr);
		ResultIconImage->SetVisibility(ESlateVisibility::Hidden);
	}
	if (ResultNameText)
	{
		ResultNameText->SetText(FText::GetEmpty());
	}
	if (ResultQuantityText)
	{
		ResultQuantityText->SetText(FText::GetEmpty());
	}
	if (MissingRecipeText)
	{
		MissingRecipeText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (RecipeDetailPanel)
	{
		RecipeDetailPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (EmptyDetailText)
	{
		EmptyDetailText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

bool UCraftingPanelWidget::RefreshSelectedRecipeDetails()
{
	if (!CraftingComponent || SelectedRecipeId.IsNone())
	{
		ClearRecipeDetails();
		return false;
	}

	FCraftingDetailsView Details;
	if (!CraftingComponent->GetCraftingDetails(SelectedRecipeId, 1, Details))
	{
		SelectedRecipeId = NAME_None;
		UpdateRecipeEntrySelection();
		ClearRecipeDetails();
		return false;
	}

	ApplyRecipeDetails(Details);
	return true;
}

void UCraftingPanelWidget::ApplyRecipeDetails(const FCraftingDetailsView& Details)
{
	SpawnedIngredientEntries.Reset();
	if (IngredientList)
	{
		IngredientList->ClearChildren();
	}

	if (RecipeDetailPanel)
	{
		RecipeDetailPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (EmptyDetailText)
	{
		EmptyDetailText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ResultNameText)
	{
		ResultNameText->SetText(Details.Header.DisplayName);
	}
	if (ResultQuantityText)
	{
		ResultQuantityText->SetText(FText::Format(
			NSLOCTEXT("Crafting", "SelectedRecipeResultQuantity", "x{0}"),
			FText::AsNumber(Details.Header.ResultQuantity)));
	}
	if (ResultIconImage)
	{
		UTexture2D* Icon = Details.Header.Icon.LoadSynchronous();
		ResultIconImage->SetBrushFromTexture(Icon, true);
		ResultIconImage->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (MissingRecipeText)
	{
		MissingRecipeText->SetVisibility(
			Details.bIngredientsVisible
				? ESlateVisibility::Collapsed
				: ESlateVisibility::HitTestInvisible);
	}
	if (IngredientList)
	{
		IngredientList->SetVisibility(
			Details.bIngredientsVisible
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
	}

	if (!Details.bIngredientsVisible || !IngredientList || !IngredientEntryClass)
	{
		return;
	}

	for (const FCraftingIngredientView& Ingredient : Details.Ingredients)
	{
		UCraftingIngredientEntryWidget* IngredientWidget =
			CreateWidget<UCraftingIngredientEntryWidget>(this, IngredientEntryClass);
		if (!IngredientWidget)
		{
			continue;
		}

		IngredientWidget->SetupFromIngredient(Ingredient);
		IngredientList->AddChild(IngredientWidget);
		SpawnedIngredientEntries.Add(IngredientWidget);
	}
}

void UCraftingPanelWidget::UpdateRecipeEntrySelection()
{
	for (UCraftingRecipeEntryWidget* EntryWidget : SpawnedRecipeEntries)
	{
		if (EntryWidget)
		{
			EntryWidget->SetSelected(EntryWidget->GetRecipeId() == SelectedRecipeId);
		}
	}
}

void UCraftingPanelWidget::HandleRecipeSelected(FName RecipeId)
{
	if (RecipeId.IsNone() || !CraftingComponent)
	{
		return;
	}

	SelectedRecipeId = RecipeId;
	UpdateRecipeEntrySelection();
	if (RefreshSelectedRecipeDetails())
	{
		OnRecipeSelected.Broadcast(SelectedRecipeId);
	}
}

void UCraftingPanelWidget::HandleCraftingDataChanged()
{
	if (bPanelActive)
	{
		RefreshRecipeList();
	}
}
