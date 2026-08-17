#include "UI/ItemQuickSlotWidget.h"

#include "BasePlayer.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Inventory/InventoryComponent.h"

namespace
{
	FText GetKeyDisplayText(const FGameplayTag& KeyTag, const int32 QuickSlotIndex)
	{
		const FString TagString = KeyTag.ToString();
		FString Left;
		FString Right;
		return TagString.Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd)
			? FText::FromString(Right)
			: FText::AsNumber(QuickSlotIndex + 1);
	}
}

void UItemQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeForPlayer(Cast<ABasePlayer>(GetOwningPlayerPawn()));
}

void UItemQuickSlotWidget::InitializeForPlayer(ABasePlayer* InPlayer)
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
		InPlayer->OnQuickSlotsChanged.AddUObject(this, &UItemQuickSlotWidget::RefreshSlots);
		InPlayer->OnConsumableQuickSlotInputChanged.AddUObject(this, &UItemQuickSlotWidget::RefreshSlots);
		if (UInventoryComponent* Inventory = InPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.AddUObject(this, &UItemQuickSlotWidget::RefreshSlots);
		}
	}

	RefreshSlots();
}

void UItemQuickSlotWidget::RefreshSlots()
{
	RefreshSlot(2, ItemIconImage3, ItemNameText3, CountText3, KeyText3, PressedHighlightBorder3, ItemInfoOverlay3);
	RefreshSlot(3, ItemIconImage4, ItemNameText4, CountText4, KeyText4, PressedHighlightBorder4, ItemInfoOverlay4);
	RefreshSlot(4, ItemIconImage5, ItemNameText5, CountText5, KeyText5, PressedHighlightBorder5, ItemInfoOverlay5);
}

void UItemQuickSlotWidget::NativeDestruct()
{
	UnbindPlayer();
	Super::NativeDestruct();
}

void UItemQuickSlotWidget::UnbindPlayer()
{
	if (ABasePlayer* Player = CachedPlayer.Get())
	{
		Player->OnQuickSlotsChanged.RemoveAll(this);
		Player->OnConsumableQuickSlotInputChanged.RemoveAll(this);
		if (UInventoryComponent* Inventory = Player->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveAll(this);
		}
	}
	CachedPlayer.Reset();
}

void UItemQuickSlotWidget::RefreshSlot(
	const int32 QuickSlotIndex,
	UImage* IconImage,
	UTextBlock* NameText,
	UTextBlock* CountText,
	UTextBlock* KeyText,
	UBorder* PressedHighlightBorder,
	UOverlay* ItemInfoOverlay) const
{
	ABasePlayer* Player = CachedPlayer.Get();
	FGameplayTag KeyTag;
	FText DisplayName = FText::GetEmpty();
	UTexture2D* Icon = nullptr;
	int32 Count = 0;
	bool bHasAssignedItem = false;

	if (Player && Player->QuickSlots.IsValidIndex(QuickSlotIndex))
	{
		const FQuickSlotReference& QuickSlot = Player->QuickSlots[QuickSlotIndex];
		KeyTag = QuickSlot.KeyTag;
		if (QuickSlot.ItemTag.IsValid())
		{
			bHasAssignedItem = true;
			if (UInventoryComponent* Inventory = Player->GetInventoryComponent())
			{
				DisplayName = Inventory->GetMaterialName(QuickSlot.ItemTag);
				Icon = Inventory->GetMaterialIcon(QuickSlot.ItemTag);
				Count = Inventory->GetMaterialCount(QuickSlot.ItemTag);
			}
		}
	}

	if (IconImage)
	{
		if (Icon)
		{
			IconImage->SetBrushFromTexture(Icon, true);
			IconImage->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			IconImage->SetColorAndOpacity(FLinearColor::Transparent);
		}
	}
	if (NameText)
	{
		NameText->SetText(DisplayName);
	}
	if (CountText)
	{
		CountText->SetText(Count > 0 ? FText::AsNumber(Count) : FText::GetEmpty());
	}
	if (KeyText)
	{
		KeyText->SetText(GetKeyDisplayText(KeyTag, QuickSlotIndex));
	}
	if (ItemInfoOverlay)
	{
		ItemInfoOverlay->SetVisibility(
			bHasAssignedItem ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (PressedHighlightBorder)
	{
		const bool bPressed = bHasAssignedItem
			&& Player
			&& Player->GetPressedConsumableQuickSlotIndex() == QuickSlotIndex;
		PressedHighlightBorder->SetVisibility(
			bPressed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
