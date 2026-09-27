#include "UI/Crafting/CraftingMenuEntryWidget.h"

#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateColorBrush.h"

void UCraftingMenuEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Button_Entry)
	{
		DefaultButtonStyle = Button_Entry->GetStyle();
		DefaultButtonStyle.SetPressedPadding(DefaultButtonStyle.NormalPadding);
		DefaultButtonStyle.SetPressed(DefaultButtonStyle.Hovered);
		bDefaultButtonStyleCached = true;
		Button_Entry->OnClicked.AddUniqueDynamic(this, &UCraftingMenuEntryWidget::HandleClicked);
	}
	RefreshVisuals();
}

void UCraftingMenuEntryWidget::NativeDestruct()
{
	if (Button_Entry)
	{
		Button_Entry->OnClicked.RemoveDynamic(this, &UCraftingMenuEntryWidget::HandleClicked);
	}
	OnRecipeActivated.Unbind();
	OnCategoryActivated.Unbind();
	Super::NativeDestruct();
}

void UCraftingMenuEntryWidget::SetupCategory(
	const FString& InCategoryPath,
	const FText& InLabel,
	int32 InDepth)
{
	RecipeId = NAME_None;
	CategoryPath = InCategoryPath;
	Label = InLabel;
	Depth = FMath::Max(0, InDepth);
	bCategory = true;
	bSelected = false;
	RefreshVisuals();
}

void UCraftingMenuEntryWidget::SetupRecipe(
	FName InRecipeId,
	const FText& InLabel,
	int32 InDepth,
	bool bInSelected)
{
	RecipeId = InRecipeId;
	CategoryPath.Reset();
	Label = InLabel;
	Depth = FMath::Max(0, InDepth);
	bCategory = false;
	bSelected = bInSelected;
	RefreshVisuals();
}

void UCraftingMenuEntryWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	RefreshVisuals();
}

void UCraftingMenuEntryWidget::RefreshVisuals()
{
	if (!Button_Entry || !WidgetSwitcher_EntryLayout || !Text_CategoryTop
		|| !Text_CategoryNested || !Text_Recipe || !SizeBox_SelectionUnderline)
	{
		return;
	}

	if (bCategory)
	{
		if (Depth == 0)
		{
			WidgetSwitcher_EntryLayout->SetActiveWidgetIndex(0);
			Text_CategoryTop->SetText(FText::Format(
				NSLOCTEXT("Crafting", "TopCategoryFormat", "<{0}>"), Label));
		}
		else
		{
			WidgetSwitcher_EntryLayout->SetActiveWidgetIndex(1);
			Text_CategoryNested->SetText(FText::Format(
				NSLOCTEXT("Crafting", "NestedCategoryFormat", "● {0}"), Label));
		}
	}
	else
	{
		WidgetSwitcher_EntryLayout->SetActiveWidgetIndex(2);
		Text_Recipe->SetText(Label);
	}
	Button_Entry->SetIsEnabled(true);
	Button_Entry->SetVisibility(ESlateVisibility::Visible);
	Button_Entry->SetBackgroundColor(FLinearColor::White);
	Text_CategoryTop->SetColorAndOpacity(FSlateColor(CategoryTextColor));
	Text_CategoryNested->SetColorAndOpacity(FSlateColor(CategoryTextColor));
	Text_Recipe->SetColorAndOpacity(FSlateColor(bSelected ? SelectedTextColor : ItemTextColor));

	if (bDefaultButtonStyleCached)
	{
		FButtonStyle EntryStyle = DefaultButtonStyle;
		EntryStyle.SetPressedPadding(EntryStyle.NormalPadding);
		if (bSelected)
		{
			const FSlateColorBrush HighlightBrush(SelectedHighlightColor);
			EntryStyle.SetNormal(HighlightBrush);
			EntryStyle.SetHovered(HighlightBrush);
			EntryStyle.SetPressed(HighlightBrush);
		}
		else if (!bCategory)
		{
			EntryStyle.SetPressed(FSlateColorBrush(SelectedHighlightColor));
		}
		Button_Entry->SetStyle(EntryStyle);
	}
	SizeBox_SelectionUnderline->SetVisibility(
		bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UCraftingMenuEntryWidget::HandleClicked()
{
	if (bCategory && !CategoryPath.IsEmpty())
	{
		OnCategoryActivated.ExecuteIfBound(CategoryPath);
	}
	else if (!RecipeId.IsNone())
	{
		OnRecipeActivated.ExecuteIfBound(RecipeId);
	}
}
