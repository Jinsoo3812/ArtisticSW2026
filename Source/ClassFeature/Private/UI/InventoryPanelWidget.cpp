// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InventoryPanelWidget.h"

#include "BasePlayer.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "UI/InventoryEntryWidget.h"

void UInventoryPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ClueTabButton)
	{
		ClueTabButton->OnClicked.AddDynamic(this, &UInventoryPanelWidget::HandleClueTabClicked);
	}
	if (ConsumableTabButton)
	{
		ConsumableTabButton->OnClicked.AddDynamic(this, &UInventoryPanelWidget::HandleConsumableTabClicked);
	}
	if (MaterialTabButton)
	{
		MaterialTabButton->OnClicked.AddDynamic(this, &UInventoryPanelWidget::HandleMaterialTabClicked);
	}
	if (WeaponTabButton)
	{
		WeaponTabButton->OnClicked.AddDynamic(this, &UInventoryPanelWidget::HandleWeaponTabClicked);
	}

	ClearItemInfo();
	RefreshTabButtonStyles();
}

void UInventoryPanelWidget::NativeDestruct()
{
	UnbindInventoryComponent();
	if (BoundStorage.IsValid()) BoundStorage->OnStorageChanged.RemoveAll(this);

	if (ClueTabButton)
	{
		ClueTabButton->OnClicked.RemoveDynamic(this, &UInventoryPanelWidget::HandleClueTabClicked);
	}
	if (ConsumableTabButton)
	{
		ConsumableTabButton->OnClicked.RemoveDynamic(this, &UInventoryPanelWidget::HandleConsumableTabClicked);
	}
	if (MaterialTabButton)
	{
		MaterialTabButton->OnClicked.RemoveDynamic(this, &UInventoryPanelWidget::HandleMaterialTabClicked);
	}
	if (WeaponTabButton)
	{
		WeaponTabButton->OnClicked.RemoveDynamic(this, &UInventoryPanelWidget::HandleWeaponTabClicked);
	}

	Super::NativeDestruct();
}

void UInventoryPanelWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	CachedPlayer = InPlayer;
	BindInventoryComponent(CachedPlayer.IsValid() ? CachedPlayer->GetInventoryComponent() : nullptr);
	RefreshInventory();
	ClearItemInfo();
	RefreshTabButtonStyles();
}

void UInventoryPanelWidget::InitializeForStorage(ABasePlayer* InPlayer, AStorageChest* InChest)
{
	if (BoundStorage.IsValid()) BoundStorage->OnStorageChanged.RemoveAll(this);
	StorageChest = InChest;
	BoundStorage = InChest ? InChest->GetStorageComponent() : nullptr;
	InitializeForPlayer(InPlayer);
	if (BoundStorage.IsValid()) BoundStorage->OnStorageChanged.AddUObject(this, &UInventoryPanelWidget::HandleInventoryChanged);
	RefreshInventory();
}

void UInventoryPanelWidget::BindInventoryComponent(UInventoryComponent* InventoryComponent)
{
	if (BoundInventoryComponent == InventoryComponent)
	{
		return;
	}

	UnbindInventoryComponent();

	BoundInventoryComponent = InventoryComponent;
	if (BoundInventoryComponent)
	{
		BoundInventoryComponent->OnInventoryChanged.AddUObject(this, &UInventoryPanelWidget::HandleInventoryChanged);
	}
}

void UInventoryPanelWidget::UnbindInventoryComponent()
{
	if (!BoundInventoryComponent)
	{
		return;
	}

	BoundInventoryComponent->OnInventoryChanged.RemoveAll(this);
	BoundInventoryComponent = nullptr;
}

void UInventoryPanelWidget::HandleInventoryChanged()
{
	RefreshInventory();
}

void UInventoryPanelWidget::RefreshInventory()
{
	if (!InventoryGridPanel)
	{
		return;
	}

	InventoryGridPanel->ClearChildren();

	if (!BoundInventoryComponent || !InventoryEntryClass)
	{
		return;
	}

	const EInventoryTab ActiveTab = BoundStorage.IsValid() ? StorageTab : BoundInventoryComponent->GetActiveTab();
	RefreshTabButtonStyles();

	const TArray<FInventorySlot>& Slots = BoundStorage.IsValid() ? BoundStorage->GetSlots() : BoundInventoryComponent->GetSlots(ActiveTab);
	const int32 Columns = BoundStorage.IsValid() ? BoundStorage->GetStorageColumns() : BoundInventoryComponent->GetInventoryColumns();
	const int32 SlotCount = BoundStorage.IsValid() ? BoundStorage->GetSlotsPerTab() : BoundInventoryComponent->GetSlotCount(ActiveTab);
	const int32 Start = BoundStorage.IsValid() ? BoundStorage->GetTabStart(ActiveTab) : 0;

	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		UInventoryEntryWidget* EntryWidget = CreateWidget<UInventoryEntryWidget>(this, InventoryEntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		const int32 DataIndex = Start + Index;
		EntryWidget->SetStorageContext(StorageChest.Get());
		if (Slots.IsValidIndex(DataIndex) && !Slots[DataIndex].IsEmpty())
		{
			const FInventorySlot& InventorySlot = Slots[DataIndex];

			EntryWidget->SetupFromData(
				BoundInventoryComponent->GetMaterialName(InventorySlot.ItemTag),
				InventorySlot.Count,
				BoundInventoryComponent->GetMaterialIcon(InventorySlot.ItemTag),
				DataIndex,
				InventorySlot.ItemTag,
				BoundInventoryComponent->GetItemRarityName(InventorySlot.ItemTag)
			);
		}
		else
		{
			EntryWidget->SetupAsEmpty(DataIndex);
		}

		EntryWidget->OnEntryHovered.BindUObject(this, &UInventoryPanelWidget::HandleInventoryEntryHovered);
		EntryWidget->OnEntryUnhovered.BindUObject(this, &UInventoryPanelWidget::HandleInventoryEntryUnhovered);

		UUniformGridSlot* GridSlot = InventoryGridPanel->AddChildToUniformGrid(
			EntryWidget,
			Index / Columns,
			Index % Columns
		);

		if (GridSlot)
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UInventoryPanelWidget::RefreshItemInfo(FGameplayTag ItemTag, int32 Count)
{
	if (!BoundInventoryComponent || !ItemTag.IsValid())
	{
		ClearItemInfo();
		return;
	}

	if (ItemInfoNameText)
	{
		ItemInfoNameText->SetText(BoundInventoryComponent->GetMaterialName(ItemTag));
	}
	if (ItemInfoDescriptionText)
	{
		ItemInfoDescriptionText->SetText(BoundInventoryComponent->GetItemDescription(ItemTag));
	}
	if (ItemInfoCountText)
	{
		ItemInfoCountText->SetText(FText::Format(NSLOCTEXT("Inventory", "ItemInfoCount", "Count: {0}"), FText::AsNumber(Count)));
	}
	if (ItemInfoRarityText)
	{
		ItemInfoRarityText->SetText(BoundInventoryComponent->GetItemRarityName(ItemTag));
	}
	if (ItemInfoIconImage)
	{
		if (UTexture2D* Icon = BoundInventoryComponent->GetMaterialIcon(ItemTag))
		{
			ItemInfoIconImage->SetBrushFromTexture(Icon, true);
			ItemInfoIconImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			ItemInfoIconImage->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

void UInventoryPanelWidget::ClearItemInfo()
{
	if (ItemInfoNameText)
	{
		ItemInfoNameText->SetText(FText::GetEmpty());
	}
	if (ItemInfoDescriptionText)
	{
		ItemInfoDescriptionText->SetText(FText::GetEmpty());
	}
	if (ItemInfoCountText)
	{
		ItemInfoCountText->SetText(FText::GetEmpty());
	}
	if (ItemInfoRarityText)
	{
		ItemInfoRarityText->SetText(FText::GetEmpty());
	}
	if (ItemInfoIconImage)
	{
		ItemInfoIconImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UInventoryPanelWidget::HandleInventoryEntryHovered(int32 SlotIndex, FGameplayTag ItemTag)
{
	if (!BoundInventoryComponent)
	{
		return;
	}

	const TArray<FInventorySlot>& Slots = BoundStorage.IsValid() ? BoundStorage->GetSlots() : BoundInventoryComponent->GetSlots(BoundInventoryComponent->GetActiveTab());
	const int32 Count = Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].Count : 0;
	RefreshItemInfo(ItemTag, Count);
}

void UInventoryPanelWidget::HandleInventoryEntryUnhovered(int32 SlotIndex)
{
	ClearItemInfo();
}

void UInventoryPanelWidget::SetInventoryTab(EInventoryTab NewTab)
{
	if (!BoundInventoryComponent)
	{
		return;
	}

	if (BoundStorage.IsValid()) StorageTab = NewTab;
	else BoundInventoryComponent->SetActiveTab(NewTab);
	ClearItemInfo();
	RefreshInventory();
	RefreshTabButtonStyles();
}

void UInventoryPanelWidget::RefreshTabButtonStyles()
{
	const EInventoryTab ActiveTab = BoundStorage.IsValid() ? StorageTab : (BoundInventoryComponent ? BoundInventoryComponent->GetActiveTab() : EInventoryTab::Material);

	ApplyTabButtonColor(ClueTabButton, ActiveTab == EInventoryTab::Clue);
	ApplyTabButtonColor(ConsumableTabButton, ActiveTab == EInventoryTab::Consumable);
	ApplyTabButtonColor(MaterialTabButton, ActiveTab == EInventoryTab::Material);
	ApplyTabButtonColor(WeaponTabButton, ActiveTab == EInventoryTab::Weapon);
}

void UInventoryPanelWidget::ApplyTabButtonColor(UButton* Button, bool bIsActive)
{
	if (!Button)
	{
		return;
	}

	FButtonStyle ButtonStyle = Button->GetStyle();
	const FLinearColor TargetColor = bIsActive ? ActiveTabColor : InactiveTabColor;
	const FSlateColor SlateColor(TargetColor);

	ButtonStyle.Normal.TintColor = SlateColor;
	ButtonStyle.Hovered.TintColor = SlateColor;
	ButtonStyle.Pressed.TintColor = SlateColor;
	ButtonStyle.Disabled.TintColor = SlateColor;

	Button->SetStyle(ButtonStyle);
}

void UInventoryPanelWidget::HandleClueTabClicked()
{
	SetInventoryTab(EInventoryTab::Clue);
}

void UInventoryPanelWidget::HandleConsumableTabClicked()
{
	SetInventoryTab(EInventoryTab::Consumable);
}

void UInventoryPanelWidget::HandleMaterialTabClicked()
{
	SetInventoryTab(EInventoryTab::Material);
}

void UInventoryPanelWidget::HandleWeaponTabClicked()
{
	SetInventoryTab(EInventoryTab::Weapon);
}
