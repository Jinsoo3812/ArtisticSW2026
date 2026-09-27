#include "UI/Crafting/CraftingCompleteWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCraftingCompleteWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddUniqueDynamic(this, &UCraftingCompleteWidget::HandleContinueClicked);
	}
}

void UCraftingCompleteWidget::NativeDestruct()
{
	if (ContinueButton)
	{
		ContinueButton->OnClicked.RemoveDynamic(this, &UCraftingCompleteWidget::HandleContinueClicked);
	}
	OnDismissed.Unbind();
	Super::NativeDestruct();
}

void UCraftingCompleteWidget::ShowCraftedItem(const FCraftingListEntry& Item)
{
	if (CraftedItemIconImage)
	{
		UTexture2D* Icon = Item.Icon.LoadSynchronous();
		CraftedItemIconImage->SetBrushFromTexture(Icon, true);
		CraftedItemIconImage->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (CraftedItemNameText)
	{
		CraftedItemNameText->SetText(Item.DisplayName);
	}
}

void UCraftingCompleteWidget::HandleContinueClicked()
{
	OnDismissed.ExecuteIfBound();
}
