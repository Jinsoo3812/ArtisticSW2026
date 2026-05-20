// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InventoryCursorWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UInventoryCursorWidget::SetupCursorItem(UTexture2D* InIcon, int32 InCount)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(InIcon, true);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	if (CountText)
	{
		CountText->SetText(FText::AsNumber(InCount));
		CountText->SetVisibility(InCount >= 1 ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UInventoryCursorWidget::ClearCursorItem()
{
	SetVisibility(ESlateVisibility::Collapsed);

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(nullptr);
	}

	if (CountText)
	{
		CountText->SetText(FText::GetEmpty());
	}
}