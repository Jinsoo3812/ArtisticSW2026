// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/PlayerHUDWidget.h"
#include "UI/QuickSlotEntryWidget.h"
#include "UI/InventoryEntryWidget.h"
#include "BasePlayer.h"
#include "Inventory/InventoryComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/Border.h"
#include "BaseItem.h"


void UPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (InventoryPanel)
	{
		InventoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UPlayerHUDWidget::NativeDestruct()
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);

		if (UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent())
		{
			InventoryComp->OnInventoryChanged.RemoveAll(this);
		}
	}

	Super::NativeDestruct();
}

void UPlayerHUDWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);

		if (UInventoryComponent* OldInventory = CachedPlayer->GetInventoryComponent())
		{
			OldInventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	CachedPlayer = InPlayer;

	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UPlayerHUDWidget::HandleItemSlotsChanged);

		if (UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent())
		{
			InventoryComp->OnInventoryChanged.AddUObject(this, &UPlayerHUDWidget::HandleInventoryChanged);
		}
	}

	RefreshQuickSlots();
	RefreshInventory();
}

void UPlayerHUDWidget::SetInventoryVisible(bool bVisible)
{
	if (!InventoryPanel)
	{
		return;
	}

	InventoryPanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (bVisible)
	{
		RefreshInventory();
	}
}

bool UPlayerHUDWidget::IsInventoryVisible() const
{
	return InventoryPanel && InventoryPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UPlayerHUDWidget::HandleInventoryChanged()
{
	RefreshInventory();
}

void UPlayerHUDWidget::HandleItemSlotsChanged()
{
	RefreshQuickSlots();
}

void UPlayerHUDWidget::RefreshQuickSlots()
{
	if (!QuickSlotBox)
	{
		return;
	}

	QuickSlotBox->ClearChildren();

	if (!CachedPlayer.IsValid() || !QuickSlotEntryClass)
	{
		return;
	}

	for (const FItemSlot& QuickSlot : CachedPlayer->ItemSlots)
	{
		UQuickSlotEntryWidget* EntryWidget = CreateWidget<UQuickSlotEntryWidget>(this, QuickSlotEntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		UTexture2D* Icon = nullptr;
		FText ItemName = FText::FromString(TEXT("Empty"));

		if (IsValid(QuickSlot.Item))
		{
			Icon = QuickSlot.Item->GetItemIcon();
			ItemName = QuickSlot.Item->GetItemNameText();
		}

		const bool bEquipped = (QuickSlot.Item == CachedPlayer->EquippedItem);

		EntryWidget->SetupFromData(QuickSlot.KeyTag, ItemName, Icon, bEquipped);
		QuickSlotBox->AddChild(EntryWidget);
	}
}

void UPlayerHUDWidget::RefreshInventory()
{
	if (!InventoryGridPanel)
	{
		return;
	}

	InventoryGridPanel->ClearChildren();

	if (!CachedPlayer.IsValid() || !InventoryEntryClass)
	{
		return;
	}

	UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent();
	if (!InventoryComp)
	{
		return;
	}

	const TArray<FInventoryMaterialEntry>& Materials = InventoryComp->GetMaterials();

	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		const FInventoryMaterialEntry& Entry = Materials[Index];

		UInventoryEntryWidget* EntryWidget = CreateWidget<UInventoryEntryWidget>(this, InventoryEntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		EntryWidget->SetupFromData(
			InventoryComp->GetMaterialName(Entry.ItemTag),
			Entry.Count,
			InventoryComp->GetMaterialIcon(Entry.ItemTag)
		);

		UUniformGridSlot* GridSlot = InventoryGridPanel->AddChildToUniformGrid(
			EntryWidget,
			Index / 4,   // Row
			Index % 4    // Column
		);

		if (GridSlot)
		{
			GridSlot->SetHorizontalAlignment(HAlign_Center);
			GridSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
}