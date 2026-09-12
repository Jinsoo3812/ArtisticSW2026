// Fill out your copyright notice in the Description page of Project Settings.


#include "InventoryEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

#include "BasePlayer.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "BasePlayerController.h"
#include "Inventory/InventoryComponent.h"

void UInventoryEntryWidget::SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex, FGameplayTag InItemTag, const FText& InRarityName)
{
	SlotIndex = InSlotIndex;
	ItemTag = InItemTag;
	DisplayedCount = InCount;
	SetToolTipText(InRarityName);

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
	ItemTag = FGameplayTag();
	DisplayedCount = 0;
	SetToolTipText(FText::GetEmpty());

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

void UInventoryEntryWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	if (SlotIndex != INDEX_NONE && ItemTag.IsValid())
	{
		OnEntryHovered.ExecuteIfBound(SlotIndex, ItemTag);
	}
}

void UInventoryEntryWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);

	if (SlotIndex != INDEX_NONE)
	{
		OnEntryUnhovered.ExecuteIfBound(SlotIndex);
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

	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer());
	if (StorageChest.IsValid())
	{
		const FKey Key = InMouseEvent.GetEffectingButton();
		if (PlayerController && (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
		{
			PlayerController->ServerSharedStorageSlotAction(StorageChest.Get(), SlotIndex, ItemTag, DisplayedCount,
				DisplayedCapacity, Key == EKeys::RightMouseButton || InMouseEvent.IsShiftDown());
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	const bool bHasOpenStorage = PlayerController && PlayerController->HasOpenStorage();

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bHasOpenStorage && InMouseEvent.IsShiftDown())
		{
			PlayerController->ServerQuickMoveInventorySlotInTab(InventoryComp->GetActiveTab(), SlotIndex, ItemTag, DisplayedCount);
			return FReply::Handled();
		}

		InventoryComp->ServerHandleLeftClickSlotInTab(InventoryComp->GetActiveTab(), SlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (InventoryComp->GetCursorItem().IsValid())
		{
			InventoryComp->ServerHandleRightClickInventory();
			return FReply::Handled();
		}

		if (bHasOpenStorage)
		{
			PlayerController->ServerQuickMoveInventorySlotInTab(InventoryComp->GetActiveTab(), SlotIndex, ItemTag, DisplayedCount);
			return FReply::Handled();
		}

		InventoryComp->ServerHandleRightClickInventory();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void UInventoryEntryWidget::SetStorageContext(AStorageChest* Chest)
{
	StorageChest = Chest;
	DisplayedCapacity = Chest ? Chest->GetStorageComponent()->GetSlotsPerTab() : 0;
}
