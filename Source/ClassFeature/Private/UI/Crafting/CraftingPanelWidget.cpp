#include "UI/Crafting/CraftingPanelWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Crafting/CraftingComponent.h"
#include "Engine/Texture2D.h"
#include "UI/Crafting/CraftingCompleteWidget.h"
#include "UI/Crafting/CraftingIngredientEntryWidget.h"

namespace CraftingPanel
{
	constexpr int32 RecipePageIndex = 0;
	constexpr int32 CompletePageIndex = 1;
}

void UCraftingPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CraftButton)
	{
		CraftButton->OnClicked.AddUniqueDynamic(this, &UCraftingPanelWidget::HandleCraftButtonClicked);
	}
	if (CraftingCompleteWidget)
	{
		CraftingCompleteWidget->OnDismissed.BindUObject(this, &UCraftingPanelWidget::HandleCraftingCompleteDismissed);
	}
	ClearRecipeDetails();
}

void UCraftingPanelWidget::NativeDestruct()
{
	if (CraftButton)
	{
		CraftButton->OnClicked.RemoveDynamic(this, &UCraftingPanelWidget::HandleCraftButtonClicked);
	}
	if (CraftingCompleteWidget)
	{
		CraftingCompleteWidget->OnDismissed.Unbind();
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
	if (!SelectedRecipeId.IsNone())
	{
		RefreshSelectedRecipeDetails();
	}
}

void UCraftingPanelWidget::DeactivateCraftingPanel()
{
	bPanelActive = false;
	UnbindCraftingEvents();
	CraftingComponent = nullptr;
	SelectedRecipeId = NAME_None;
	SelectedRecipeHeader = FCraftingListEntry();
	PendingRequestId.Invalidate();
	bCraftRequestPending = false;
	ClearRecipeDetails();
}

bool UCraftingPanelWidget::SelectRecipe(FName RecipeId)
{
	if (!bPanelActive || !CraftingComponent || RecipeId.IsNone())
	{
		return false;
	}
	SelectedRecipeId = RecipeId;
	if (!RefreshSelectedRecipeDetails())
	{
		SelectedRecipeId = NAME_None;
		return false;
	}
	OnRecipeSelected.Broadcast(SelectedRecipeId);
	return true;
}

void UCraftingPanelWidget::RefreshSelectedRecipe()
{
	RefreshSelectedRecipeDetails();
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

void UCraftingPanelWidget::ClearRecipeDetails()
{
	SpawnedIngredientEntries.Reset();
	for (UPanelWidget* IngredientHost : {IngredientNorthSlot.Get(), IngredientEastSlot.Get(), IngredientSouthSlot.Get(), IngredientWestSlot.Get()})
	{
		if (IngredientHost)
		{
			IngredientHost->ClearChildren();
			IngredientHost->SetVisibility(ESlateVisibility::Hidden);
		}
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
	if (CraftResultText)
	{
		CraftResultText->SetText(FText::GetEmpty());
		CraftResultText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetCraftButtonAvailable(false);
	if (RecipeDetailPanel)
	{
		RecipeDetailPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (EmptyDetailText)
	{
		EmptyDetailText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (WidgetSwitcher_CraftingState)
	{
		WidgetSwitcher_CraftingState->SetActiveWidgetIndex(CraftingPanel::RecipePageIndex);
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
		ClearRecipeDetails();
		return false;
	}
	ApplyRecipeDetails(Details);
	return true;
}

void UCraftingPanelWidget::ApplyRecipeDetails(const FCraftingDetailsView& Details)
{
	SelectedRecipeHeader = Details.Header;
	SpawnedIngredientEntries.Reset();
	TArray<UPanelWidget*> DirectionSlots = {
		IngredientNorthSlot.Get(), IngredientEastSlot.Get(), IngredientSouthSlot.Get(), IngredientWestSlot.Get()};
	for (UPanelWidget* IngredientHost : DirectionSlots)
	{
		if (IngredientHost)
		{
			IngredientHost->ClearChildren();
			IngredientHost->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	if (WidgetSwitcher_CraftingState)
	{
		WidgetSwitcher_CraftingState->SetActiveWidgetIndex(CraftingPanel::RecipePageIndex);
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
			Details.Availability == ECraftingAvailability::MissingRecipe
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}

	const int32 IngredientCount = FMath::Min(Details.Ingredients.Num(), ArtisticCrafting::MaxIngredientSlots);
	if (Details.bIngredientsVisible && IngredientEntryClass)
	{
		for (int32 Index = 0; Index < IngredientCount; ++Index)
		{
			UPanelWidget* TargetSlot = DirectionSlots[Index];
			if (!TargetSlot)
			{
				continue;
			}
			UCraftingIngredientEntryWidget* Entry = CreateWidget<UCraftingIngredientEntryWidget>(this, IngredientEntryClass);
			if (!Entry)
			{
				continue;
			}
			Entry->SetupFromIngredient(Details.Ingredients[Index]);
			TargetSlot->AddChild(Entry);
			TargetSlot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			SpawnedIngredientEntries.Add(Entry);
		}
	}
	SetCraftButtonAvailable(
		!bCraftRequestPending
		&& Details.Ingredients.Num() <= ArtisticCrafting::MaxIngredientSlots
		&& Details.Availability == ECraftingAvailability::Available);
}

void UCraftingPanelWidget::SetCraftButtonAvailable(bool bAvailable)
{
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(bAvailable);
	}
	if (CraftButtonText)
	{
		CraftButtonText->SetColorAndOpacity(FSlateColor(
			bAvailable ? EnabledCraftTextColor : DisabledCraftTextColor));
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
		|| Details.Ingredients.Num() > ArtisticCrafting::MaxIngredientSlots
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
	SetCraftButtonAvailable(false);
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
	if (Result.Reason == ECraftingFailureReason::Success && CraftingCompleteWidget && WidgetSwitcher_CraftingState)
	{
		CraftingCompleteWidget->ShowCraftedItem(SelectedRecipeHeader);
		WidgetSwitcher_CraftingState->SetActiveWidgetIndex(CraftingPanel::CompletePageIndex);
		return;
	}
	if (CraftResultText)
	{
		CraftResultText->SetText(GetCraftResultText(Result.Reason));
		CraftResultText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	RefreshSelectedRecipeDetails();
}

void UCraftingPanelWidget::HandleCraftingCompleteDismissed()
{
	if (WidgetSwitcher_CraftingState)
	{
		WidgetSwitcher_CraftingState->SetActiveWidgetIndex(CraftingPanel::RecipePageIndex);
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
	default:
		return NSLOCTEXT("Crafting", "CraftResultFailed", "제작에 실패했습니다.");
	}
}

void UCraftingPanelWidget::HandleCraftingDataChanged()
{
	if (bPanelActive && !SelectedRecipeId.IsNone()
		&& (!WidgetSwitcher_CraftingState
			|| WidgetSwitcher_CraftingState->GetActiveWidgetIndex() == CraftingPanel::RecipePageIndex))
	{
		RefreshSelectedRecipeDetails();
	}
}
