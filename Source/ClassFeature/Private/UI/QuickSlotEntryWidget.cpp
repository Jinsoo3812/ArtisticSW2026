// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/QuickSlotEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/Texture2D.h"

void UQuickSlotEntryWidget::SetupFromData(const FGameplayTag& InSlotTag, const FText& InItemName, UTexture2D* InIcon, bool bEquipped)
{
	if (SlotText)
	{
		FString SlotString = InSlotTag.ToString();
		FString Left, Right;
		if (SlotString.Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			SlotText->SetText(FText::FromString(Right));
		}
		else
		{
			SlotText->SetText(FText::FromString(SlotString));
		}
	}

	if (ItemNameText)
	{
		ItemNameText->SetText(InItemName);
	}

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(InIcon, true);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	if (EquippedBorder)
	{
		EquippedBorder->SetVisibility(bEquipped ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}