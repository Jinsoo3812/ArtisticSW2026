#include "UI/WeaponQuickSlotWidget.h"

#include "BaseItem.h"
#include "BasePlayer.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Inventory/InventoryComponent.h"

void UWeaponQuickSlotWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.Get() == InPlayer)
	{
		RefreshSlots();
		return;
	}

	UnbindPlayer();
	CachedPlayer = InPlayer;

	if (InPlayer)
	{
		InPlayer->OnQuickSlotsChanged.AddUObject(this, &UWeaponQuickSlotWidget::RefreshSlots);
		InPlayer->OnItemSlotsChanged.AddUObject(this, &UWeaponQuickSlotWidget::RefreshSlots);
	}

	RefreshSlots();
}

void UWeaponQuickSlotWidget::RefreshSlots()
{
	RefreshSlot(0, WeaponImage1, SlotNumber1);
	RefreshSlot(1, WeaponImage2, SlotNumber2);
}

void UWeaponQuickSlotWidget::NativeDestruct()
{
	UnbindPlayer();
	Super::NativeDestruct();
}

void UWeaponQuickSlotWidget::UnbindPlayer()
{
	if (ABasePlayer* Player = CachedPlayer.Get())
	{
		Player->OnQuickSlotsChanged.RemoveAll(this);
		Player->OnItemSlotsChanged.RemoveAll(this);
	}

	CachedPlayer.Reset();
}

void UWeaponQuickSlotWidget::RefreshSlot(
	const int32 QuickSlotIndex,
	UImage* WeaponImage,
	UImage* SlotNumber) const
{
	ABasePlayer* Player = CachedPlayer.Get();
	UTexture2D* WeaponIcon = nullptr;
	bool bSelected = false;

	if (Player && Player->QuickSlots.IsValidIndex(QuickSlotIndex))
	{
		const FQuickSlotReference& QuickSlot = Player->QuickSlots[QuickSlotIndex];
		if (QuickSlot.SlotType == EQuickSlotType::Weapon && QuickSlot.ItemTag.IsValid())
		{
			if (UInventoryComponent* Inventory = Player->GetInventoryComponent())
			{
				WeaponIcon = Inventory->GetMaterialIcon(QuickSlot.ItemTag);
			}

			bSelected = IsValid(Player->EquippedItem)
				&& Player->EquippedItem->ItemTag.MatchesTagExact(QuickSlot.ItemTag);
		}
	}

	if (WeaponImage)
	{
		if (WeaponIcon)
		{
			WeaponImage->SetBrushFromTexture(WeaponIcon, true);
			WeaponImage->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			WeaponImage->SetColorAndOpacity(FLinearColor::Transparent);
		}
	}

	if (SlotNumber)
	{
		SlotNumber->SetVisibility(ESlateVisibility::HitTestInvisible);
		SlotNumber->SetColorAndOpacity(bSelected ? SelectedSlotNumberColor : DefaultSlotNumberColor);
	}
}
