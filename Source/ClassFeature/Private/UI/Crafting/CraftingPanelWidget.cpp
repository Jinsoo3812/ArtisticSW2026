#include "UI/Crafting/CraftingPanelWidget.h"

#include "Components/Button.h"
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

void UCraftingPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// The teammate-authored WBP shipped with a lowercase "l" in Ingredientlist.
	// Keep the asset functional without forcing a binary Blueprint rename during integration.
	if (!IngredientList)
	{
		IngredientList = Cast<UPanelWidget>(GetWidgetFromName(TEXT("Ingredientlist")));
	}

	if (CraftButton)
	{
		CraftButton->OnClicked.AddUniqueDynamic(this, &UCraftingPanelWidget::HandleCraftButtonClicked);
		CraftButton->SetIsEnabled(false);
	}
}

void UCraftingPanelWidget::NativeDestruct()
{
	if (CraftButton)
	{
		CraftButton->OnClicked.RemoveDynamic(this, &UCraftingPanelWidget::HandleCraftButtonClicked);
	}

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
	PendingRequestId.Invalidate();
	bCraftRequestPending = false;
	ClearRecipeList();
	ClearRecipeDetails();
}

void UCraftingPanelWidget::BindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.AddUniqueDynamic(this, &UCraftingPanelWidget::HandleCraftingDataChanged);
		CraftingComponent->OnCraftingResult.AddUniqueDynamic(this, &UCraftingPanelWidget::HandleCraftingResult);
	}
}

void UCraftingPanelWidget::UnbindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.RemoveDynamic(this, &UCraftingPanelWidget::HandleCraftingDataChanged);
		CraftingComponent->OnCraftingResult.RemoveDynamic(this, &UCraftingPanelWidget::HandleCraftingResult);
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
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(false);
	}
	if (CraftResultText)
	{
		CraftResultText->SetText(FText::GetEmpty());
		CraftResultText->SetVisibility(ESlateVisibility::Collapsed);
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
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(
			!bCraftRequestPending
			&& Details.Availability == ECraftingAvailability::Available);
	}

	if (MissingRecipeText)
	{
		MissingRecipeText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IngredientList)
	{
		IngredientList->SetVisibility(
			(Details.bHasRequiredRecipeItem || Details.bIngredientsVisible)
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
	}

	if (!IngredientList || !IngredientEntryClass)
	{
		return;
	}

	const auto AddIngredientEntry = [this](const FCraftingIngredientView& Ingredient)
	{
		UCraftingIngredientEntryWidget* IngredientWidget =
			CreateWidget<UCraftingIngredientEntryWidget>(this, IngredientEntryClass);
		if (!IngredientWidget)
		{
			return;
		}

		IngredientWidget->SetupFromIngredient(Ingredient);
		IngredientList->AddChild(IngredientWidget);
		SpawnedIngredientEntries.Add(IngredientWidget);
	};

	if (Details.bHasRequiredRecipeItem)
	{
		AddIngredientEntry(Details.RequiredRecipeItem);
	}

	if (!Details.bIngredientsVisible)
	{
		return;
	}

	for (const FCraftingIngredientView& Ingredient : Details.Ingredients)
	{
		AddIngredientEntry(Ingredient);
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
	if (CraftResultText)
	{
		CraftResultText->SetText(FText::GetEmpty());
		CraftResultText->SetVisibility(ESlateVisibility::Collapsed);
	}
	UpdateRecipeEntrySelection();
	if (RefreshSelectedRecipeDetails())
	{
		OnRecipeSelected.Broadcast(SelectedRecipeId);
	}
}

void UCraftingPanelWidget::HandleCraftButtonClicked()
{
	if (!bPanelActive || !CraftingComponent || SelectedRecipeId.IsNone() || bCraftRequestPending)
	{
		return;
	}

	FCraftingDetailsView Details;
	if (!CraftingComponent->GetCraftingDetails(SelectedRecipeId, 1, Details)
		|| Details.Availability != ECraftingAvailability::Available)
	{
		RefreshSelectedRecipeDetails();
		return;
	}

	FCraftingRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.RecipeId = SelectedRecipeId;
	Request.CraftCount = 1;
	Request.Output.Type = ECraftingOutputType::Inventory;

	PendingRequestId = Request.RequestId;
	bCraftRequestPending = true;
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(false);
	}
	if (CraftResultText)
	{
		CraftResultText->SetText(FText::GetEmpty());
		CraftResultText->SetVisibility(ESlateVisibility::Collapsed);
	}

	CraftingComponent->RequestCraft(Request);
}

void UCraftingPanelWidget::HandleCraftingResult(const FCraftingResult& Result)
{
	if (!bCraftRequestPending || Result.RequestId != PendingRequestId)
	{
		return;
	}

	bCraftRequestPending = false;
	PendingRequestId.Invalidate();

	if (CraftResultText)
	{
		CraftResultText->SetText(GetCraftResultText(Result.Reason));
		CraftResultText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	RefreshSelectedRecipeDetails();
}

FText UCraftingPanelWidget::GetCraftResultText(ECraftingFailureReason FailureReason)
{
	switch (FailureReason)
	{
	case ECraftingFailureReason::Success:
		return NSLOCTEXT("Crafting", "CraftResultSuccess", "제작 완료");
	case ECraftingFailureReason::MissingRecipe:
		return NSLOCTEXT("Crafting", "CraftResultMissingRecipe", "필요한 제작법이 없습니다.");
	case ECraftingFailureReason::MissingIngredients:
		return NSLOCTEXT("Crafting", "CraftResultMissingIngredients", "재료가 부족합니다.");
	case ECraftingFailureReason::OutputUnavailable:
		return NSLOCTEXT("Crafting", "CraftResultOutputUnavailable", "결과 아이템을 넣을 공간이 없습니다.");
	case ECraftingFailureReason::NoActiveContext:
	case ECraftingFailureReason::OutOfRange:
		return NSLOCTEXT("Crafting", "CraftResultInvalidContext", "제작 시설을 사용할 수 없습니다.");
	case ECraftingFailureReason::RecipeDisabled:
	case ECraftingFailureReason::InvalidRecipe:
		return NSLOCTEXT("Crafting", "CraftResultInvalidRecipe", "사용할 수 없는 제작법입니다.");
	case ECraftingFailureReason::DuplicateRequest:
		return NSLOCTEXT("Crafting", "CraftResultDuplicate", "이미 처리된 제작 요청입니다.");
	case ECraftingFailureReason::InvalidQuantity:
	case ECraftingFailureReason::OutputRejected:
	case ECraftingFailureReason::InternalError:
	default:
		return NSLOCTEXT("Crafting", "CraftResultFailed", "제작에 실패했습니다.");
	}
}

void UCraftingPanelWidget::HandleCraftingDataChanged()
{
	if (bPanelActive)
	{
		RefreshRecipeList();
	}
}
