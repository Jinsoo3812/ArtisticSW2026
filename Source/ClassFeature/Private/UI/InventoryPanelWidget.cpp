// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InventoryPanelWidget.h"

#include "BasePlayer.h"
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

	TrySelectFirstNonEmptyTab();

	const EInventoryTab ActiveTab = BoundInventoryComponent->GetActiveTab();
	RefreshTabButtonStyles();

	const TArray<FInventorySlot>& Slots = BoundInventoryComponent->GetSlots(ActiveTab);
	const int32 Columns = BoundInventoryComponent->GetInventoryColumns();
	const int32 SlotCount = BoundInventoryComponent->GetSlotCount(ActiveTab);

	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		UInventoryEntryWidget* EntryWidget = CreateWidget<UInventoryEntryWidget>(this, InventoryEntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		if (Slots.IsValidIndex(Index) && !Slots[Index].IsEmpty())
		{
			const FInventorySlot& InventorySlot = Slots[Index];

			EntryWidget->SetupFromData(
				BoundInventoryComponent->GetMaterialName(InventorySlot.ItemTag),
				InventorySlot.Count,
				BoundInventoryComponent->GetMaterialIcon(InventorySlot.ItemTag),
				Index,
				InventorySlot.ItemTag,
				BoundInventoryComponent->GetItemRarityName(InventorySlot.ItemTag)
			);
		}
		else
		{
			EntryWidget->SetupAsEmpty(Index);
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

bool UInventoryPanelWidget::TrySelectFirstNonEmptyTab()
{
	if (!BoundInventoryComponent || HasAnyItemInTab(BoundInventoryComponent->GetActiveTab()))
	{
		return false;
	}

	const EInventoryTab Tabs[] =
	{
		EInventoryTab::Clue,
		EInventoryTab::Consumable,
		EInventoryTab::Material,
		EInventoryTab::Weapon
	};

	for (const EInventoryTab Tab : Tabs)
	{
		if (HasAnyItemInTab(Tab))
		{
			BoundInventoryComponent->SetActiveTab(Tab);
			return true;
		}
	}

	return false;
}

bool UInventoryPanelWidget::HasAnyItemInTab(EInventoryTab Tab) const
{
	if (!BoundInventoryComponent)
	{
		return false;
	}

	for (const FInventorySlot& InventorySlot : BoundInventoryComponent->GetSlots(Tab))
	{
		if (!InventorySlot.IsEmpty())
		{
			return true;
		}
	}

	return false;
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

	const TArray<FInventorySlot>& Slots = BoundInventoryComponent->GetSlots(BoundInventoryComponent->GetActiveTab());
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

	BoundInventoryComponent->SetActiveTab(NewTab);
	ClearItemInfo();
	RefreshInventory();
	RefreshTabButtonStyles();
}

void UInventoryPanelWidget::RefreshTabButtonStyles()
{
	const EInventoryTab ActiveTab = BoundInventoryComponent ? BoundInventoryComponent->GetActiveTab() : EInventoryTab::Material;

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
