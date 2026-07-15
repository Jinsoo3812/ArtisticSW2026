#include "UI/StatusWindowWidget.h"

#include "BaseItem.h"
#include "BasePlayer.h"
#include "BaseGameplayTags.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Inventory/InventoryComponent.h"
#include "UI/CharacterPreviewWidget.h"
#include "UI/InventoryPanelWidget.h"
#include "UI/QuickSlotEntryWidget.h"

void UStatusWindowWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);
		if (UInventoryComponent* OldInventory = CachedPlayer->GetInventoryComponent())
		{
			OldInventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	CachedPlayer = InPlayer;
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UStatusWindowWidget::RefreshQuickSlots);
		CachedPlayer->OnQuickSlotsChanged.AddUObject(this, &UStatusWindowWidget::RefreshQuickSlots);
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.AddUObject(this, &UStatusWindowWidget::HandleInventoryChanged);
		}
	}

	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->InitializeForPlayer(CachedPlayer.Get());
	}
	if (CharacterPreviewWidget)
	{
		CharacterPreviewWidget->InitializeForPlayer(CachedPlayer.Get());
	}
	RefreshQuickSlots();
}

void UStatusWindowWidget::SetStatusVisible(bool bVisible)
{
	if (CharacterPreviewWidget)
	{
		CharacterPreviewWidget->SetPreviewActive(bVisible);
	}

	if (!bVisible && CachedPlayer.IsValid())
	{
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->ServerHandleRightClickInventory();
		}
	}

	SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bVisible)
	{
		if (InventoryPanelWidget)
		{
			InventoryPanelWidget->RefreshInventory();
		}
		if (CharacterPreviewWidget)
		{
			CharacterPreviewWidget->RefreshPreview();
		}
		RefreshQuickSlots();
	}
}

bool UStatusWindowWidget::IsStatusVisible() const
{
	return GetVisibility() != ESlateVisibility::Collapsed;
}

void UStatusWindowWidget::SetLevelValue(int32 Level)
{
	if (LevelText)
	{
		LevelText->SetText(FText::AsNumber(Level));
	}
}

void UStatusWindowWidget::SetAttackSpeedValue(float AttackSpeed)
{
	if (AttackSpeedText)
	{
		AttackSpeedText->SetText(FText::AsNumber(AttackSpeed));
	}
}

void UStatusWindowWidget::SetExperienceValues(float CurrentExperience, float RequiredExperience)
{
	if (ExperienceText)
	{
		ExperienceText->SetText(FText::Format(NSLOCTEXT("Status", "ExperienceFormat", "{0} / {1}"),
			FText::AsNumber(CurrentExperience), FText::AsNumber(RequiredExperience)));
	}
	if (ExperienceProgressBar)
	{
		ExperienceProgressBar->SetPercent(RequiredExperience > 0.0f
			? FMath::Clamp(CurrentExperience / RequiredExperience, 0.0f, 1.0f)
			: 0.0f);
	}
}

void UStatusWindowWidget::RefreshQuickSlots()
{
	if (!CachedPlayer.IsValid())
	{
		return;
	}

	UQuickSlotEntryWidget* Entries[] =
	{
		WeaponQuickSlot1, WeaponQuickSlot2, ConsumableQuickSlot3, ConsumableQuickSlot4, ConsumableQuickSlot5
	};
	const FGameplayTag FallbackTags[] = { Key_Item_1, Key_Item_2, Key_Item_3, Key_Item_4, Key_Item_5 };
	UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent();

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
	{
		UQuickSlotEntryWidget* Entry = Entries[Index];
		if (!Entry)
		{
			continue;
		}

		FGameplayTag SlotKeyTag = FallbackTags[Index];
		FText ItemName;
		UTexture2D* Icon = nullptr;
		int32 Count = 0;
		bool bEquipped = false;
		if (CachedPlayer->QuickSlots.IsValidIndex(Index))
		{
			const FQuickSlotReference& QuickSlot = CachedPlayer->QuickSlots[Index];
			SlotKeyTag = QuickSlot.KeyTag.IsValid() ? QuickSlot.KeyTag : SlotKeyTag;
			if (Inventory && QuickSlot.ItemTag.IsValid())
			{
				ItemName = Inventory->GetMaterialName(QuickSlot.ItemTag);
				Icon = Inventory->GetMaterialIcon(QuickSlot.ItemTag);
				Count = Inventory->GetMaterialCount(QuickSlot.ItemTag);
				bEquipped = IsValid(CachedPlayer->EquippedItem) && CachedPlayer->EquippedItem->ItemTag == QuickSlot.ItemTag;
			}
		}

		Entry->ConfigureInteraction(Index, true);
		Entry->SetupFromData(SlotKeyTag, ItemName, Icon, bEquipped, Count);
	}
}

void UStatusWindowWidget::HandleInventoryChanged()
{
	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->RefreshInventory();
	}
	RefreshQuickSlots();
}

void UStatusWindowWidget::NativeDestruct()
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveAll(this);
		}
	}
	Super::NativeDestruct();
}
