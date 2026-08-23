#include "UI/Crafting/CraftingMenuEntryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Misc/Paths.h"
#include "Brushes/SlateColorBrush.h"

void UCraftingMenuEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UCraftingMenuEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	if (Button_Entry)
	{
		DefaultButtonStyle = Button_Entry->GetStyle();
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
	Super::NativeDestruct();
}

void UCraftingMenuEntryWidget::SetupCategory(const FText& InLabel, int32 InDepth)
{
	RecipeId = NAME_None;
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

void UCraftingMenuEntryWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_EntryRoot"));
	Root->SetMinDesiredHeight(24.0f);
	WidgetTree->RootWidget = Root;

	Button_Entry = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Entry"));
	Root->AddChild(Button_Entry);

	Text_Entry = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Entry"));
	Text_Entry->SetFont(FSlateFontInfo(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"),
		14.0f));
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button_Entry->AddChild(Text_Entry)))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Left);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UCraftingMenuEntryWidget::RefreshVisuals()
{
	if (!Button_Entry || !Text_Entry)
	{
		return;
	}

	const float LeftPadding = 8.0f + static_cast<float>(Depth) * 12.0f;
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Text_Entry->Slot))
	{
		TextSlot->SetPadding(FMargin(LeftPadding, 2.0f, 4.0f, 2.0f));
	}

	Text_Entry->SetText(Label);
	Button_Entry->SetIsEnabled(true);
	Button_Entry->SetVisibility(bCategory ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible);
	Button_Entry->SetBackgroundColor(FLinearColor::White);
	Text_Entry->SetColorAndOpacity(FSlateColor(
		bSelected ? SelectedTextColor : (bCategory ? CategoryTextColor : ItemTextColor)));

	if (bDefaultButtonStyleCached)
	{
		FButtonStyle EntryStyle = DefaultButtonStyle;
		if (bSelected)
		{
			const FLinearColor HoveredHighlight(
				SelectedHighlightColor.R,
				SelectedHighlightColor.G,
				SelectedHighlightColor.B,
				FMath::Min(SelectedHighlightColor.A + 0.10f, 1.0f));
			const FLinearColor PressedHighlight(
				SelectedHighlightColor.R,
				SelectedHighlightColor.G,
				SelectedHighlightColor.B,
				FMath::Min(SelectedHighlightColor.A + 0.20f, 1.0f));
			EntryStyle.SetNormal(FSlateColorBrush(SelectedHighlightColor));
			EntryStyle.SetHovered(FSlateColorBrush(HoveredHighlight));
			EntryStyle.SetPressed(FSlateColorBrush(PressedHighlight));
		}
		Button_Entry->SetStyle(EntryStyle);
	}
}

void UCraftingMenuEntryWidget::HandleClicked()
{
	if (!bCategory && !RecipeId.IsNone())
	{
		OnRecipeActivated.ExecuteIfBound(RecipeId);
	}
}
