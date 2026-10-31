#include "UI/ItemQuickSlotWidget.h"
#include "UI/ItemIconLibrary.h"

#include "BasePlayer.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Inventory/InventoryComponent.h"
#include "UI/InventoryItemDragDropOperation.h"

void UItemQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Visible);
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
	RefreshSlot(2, ItemIconImage3, ItemNameText3, CountText3, PressedHighlightBorder3, ItemInfoOverlay3);
	RefreshSlot(3, ItemIconImage4, ItemNameText4, CountText4, PressedHighlightBorder4, ItemInfoOverlay4);
	RefreshSlot(4, ItemIconImage5, ItemNameText5, CountText5, PressedHighlightBorder5, ItemInfoOverlay5);
}

void UItemQuickSlotWidget::NativeDestruct()
{
	UnbindPlayer();
	Super::NativeDestruct();
}

bool UItemQuickSlotWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const UInventoryItemDragDropOperation* InventoryOperation = Cast<UInventoryItemDragDropOperation>(InOperation);
	return InventoryOperation && InventoryOperation->ItemTag.IsValid();
}

bool UItemQuickSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const UInventoryItemDragDropOperation* InventoryOperation = Cast<UInventoryItemDragDropOperation>(InOperation);
	ABasePlayer* Player = CachedPlayer.Get();
	if (!InventoryOperation || !InventoryOperation->ItemTag.IsValid() || !Player)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	UE_LOG(LogTemp, Display, TEXT("[QuickSlotDrag] Event=HUDDropReceived Item=%s"),
		*InventoryOperation->ItemTag.ToString());
	return AssignItemAtScreenPosition(InDragDropEvent.GetScreenSpacePosition(), InventoryOperation->ItemTag);
}

FReply UItemQuickSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	ABasePlayer* Player = CachedPlayer.Get();
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Inventory)
	{
		const FInventoryCursorItem& CursorItem = Inventory->GetCursorItem();
		if (CursorItem.IsValid()
			&& AssignItemAtScreenPosition(InMouseEvent.GetScreenSpacePosition(), CursorItem.ItemTag))
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UItemQuickSlotWidget::AssignItemAtScreenPosition(const FVector2D& ScreenPosition, FGameplayTag ItemTag)
{
	ABasePlayer* Player = CachedPlayer.Get();
	if (!Player || !ItemTag.IsValid())
	{
		return false;
	}

	struct FDropTarget
	{
		UWidget* SlotArea;
		UWidget* IconArea;
		int32 QuickSlotIndex;
	};

	const FDropTarget DropTargets[] =
	{
		{ ItemInfoOverlay3, ItemIconImage3, 2 },
		{ ItemInfoOverlay4, ItemIconImage4, 3 },
		{ ItemInfoOverlay5, ItemIconImage5, 4 }
	};
	const auto IsInsideWidget = [&ScreenPosition](const UWidget* Widget)
	{
		if (!Widget)
		{
			return false;
		}

		const FGeometry& WidgetGeometry = Widget->GetCachedGeometry();
		const FVector2D LocalPosition = WidgetGeometry.AbsoluteToLocal(ScreenPosition);
		const FVector2D LocalSize = WidgetGeometry.GetLocalSize();
		return LocalSize.X > 0.f && LocalSize.Y > 0.f
			&& LocalPosition.X >= 0.f && LocalPosition.Y >= 0.f
			&& LocalPosition.X <= LocalSize.X && LocalPosition.Y <= LocalSize.Y;
	};

	for (const FDropTarget& Target : DropTargets)
	{
		if (!IsInsideWidget(Target.SlotArea) && !IsInsideWidget(Target.IconArea))
		{
			continue;
		}

		if (!Player->CanQuickSlotAcceptItem(Target.QuickSlotIndex, ItemTag))
		{
			UE_LOG(LogTemp, Display, TEXT("[QuickSlotDrag] Event=HUDDropRejected Item=%s QuickSlot=%d"),
				*ItemTag.ToString(), Target.QuickSlotIndex + 1);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[QuickSlotDrag] Event=HUDDropAccepted Item=%s QuickSlot=%d"),
			*ItemTag.ToString(), Target.QuickSlotIndex + 1);
		Player->AssignQuickSlotItem(Target.QuickSlotIndex, ItemTag);
		return true;
	}

	UE_LOG(LogTemp, Display, TEXT("[QuickSlotDrag] Event=HUDDropOutsideSlots Item=%s"),
		*ItemTag.ToString());
	return false;
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
	UBorder* PressedHighlightBorder,
	UOverlay* ItemInfoOverlay) const
{
	ABasePlayer* Player = CachedPlayer.Get();
	FText DisplayName = FText::GetEmpty();
	UTexture2D* Icon = nullptr;
	FGameplayTag IconItemTag;
	int32 Count = 0;
	bool bHasAssignedItem = false;

	if (Player && Player->QuickSlots.IsValidIndex(QuickSlotIndex))
	{
		const FQuickSlotReference& QuickSlot = Player->QuickSlots[QuickSlotIndex];
		if (QuickSlot.ItemTag.IsValid())
		{
			bHasAssignedItem = true;
			IconItemTag = QuickSlot.ItemTag;
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
			UItemIconLibrary::SetItemIcon(IconImage, Icon, IconItemTag);
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
	if (ItemInfoOverlay)
	{
		ItemInfoOverlay->SetVisibility(
			bHasAssignedItem ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (PressedHighlightBorder)
	{
		const bool bPressed = bHasAssignedItem
			&& Player
			&& Player->GetSelectedConsumableQuickSlotIndex() == QuickSlotIndex;
		PressedHighlightBorder->SetVisibility(
			bPressed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
