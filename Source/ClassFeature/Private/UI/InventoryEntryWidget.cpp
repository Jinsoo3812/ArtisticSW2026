// Fill out your copyright notice in the Description page of Project Settings.


#include "InventoryEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UInventoryEntryWidget::SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon)
{
	if (ItemNameText)
	{
		ItemNameText->SetText(InItemName);
	}

	if (CountText)
	{
		CountText->SetText(FText::AsNumber(InCount));
	}

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(InIcon, true);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}
