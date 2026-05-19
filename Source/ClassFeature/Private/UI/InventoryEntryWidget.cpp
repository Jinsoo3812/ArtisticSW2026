// Fill out your copyright notice in the Description page of Project Settings.


#include "InventoryEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

#include "BasePlayer.h"
#include "Inventory/InventoryComponent.h"

void UInventoryEntryWidget::SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex)
{
	SlotIndex = InSlotIndex;

	if (ItemNameText)
	{
		ItemNameText->SetText(InItemName);
		ItemNameText->SetVisibility(ESlateVisibility::Visible);
	}

	if (CountText)
	{
		CountText->SetText(FText::AsNumber(InCount));
		CountText->SetVisibility(ESlateVisibility::Visible);
	}

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(InIcon, true);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UInventoryEntryWidget::SetupAsEmpty(int32 InSlotIndex)
{
	SlotIndex = InSlotIndex;

	if (ItemNameText)
	{
		ItemNameText->SetText(FText::GetEmpty());
		ItemNameText->SetVisibility(ESlateVisibility::Hidden);
	}

	if (CountText)
	{
		CountText->SetText(FText::GetEmpty());
		CountText->SetVisibility(ESlateVisibility::Hidden);
	}

	if (ItemIconImage)
	{
		ItemIconImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

FReply UInventoryEntryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (SlotIndex == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetOwningPlayerPawn());
	if (!Player)
	{
		return FReply::Unhandled();
	}

	UInventoryComponent* InventoryComp = Player->GetInventoryComponent();
	if (!InventoryComp)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		InventoryComp->ServerHandleLeftClickSlot(SlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		InventoryComp->ServerHandleRightClickInventory();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}