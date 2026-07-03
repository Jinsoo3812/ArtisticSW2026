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
#include "UI/InventoryCursorWidget.h"
#include "UI/HealthBarWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/BaseHealthComponent.h"

#include "BaseGameplayTags.h"

#include "BaseItem.h"


void UPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (InventoryPanel)
	{
		InventoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (RootCanvasPanel && InventoryCursorWidgetClass && !InventoryCursorWidget)
	{
		InventoryCursorWidget = CreateWidget<UInventoryCursorWidget>(this, InventoryCursorWidgetClass);

		if (InventoryCursorWidget)
		{
			RootCanvasPanel->AddChild(InventoryCursorWidget);

			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(InventoryCursorWidget->Slot))
			{
				CanvasSlot->SetAutoSize(true);
				CanvasSlot->SetZOrder(999);
			}

			InventoryCursorWidget->ClearCursorItem();
		}
	}
}

void UPlayerHUDWidget::NativeDestruct()
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.RemoveAll(this);
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);

		if (UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent())
		{
			InventoryComp->OnInventoryChanged.RemoveAll(this);
		}
	}

	UnbindHealthComponent();

	Super::NativeDestruct();
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshCursorItemWidget();
	UpdateCursorItemWidgetPosition();
}

void UPlayerHUDWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.RemoveAll(this);
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);

		if (UInventoryComponent* OldInventory = CachedPlayer->GetInventoryComponent())
		{
			OldInventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	UnbindHealthComponent();

	CachedPlayer = InPlayer;

	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.AddUObject(this, &UPlayerHUDWidget::HandleAbilitySystemInitialized);
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UPlayerHUDWidget::HandleItemSlotsChanged);

		if (UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent())
		{
			InventoryComp->OnInventoryChanged.AddUObject(this, &UPlayerHUDWidget::HandleInventoryChanged);
		}

		BindHealthComponent(CachedPlayer->GetHealthComponent());
	}

	RefreshQuickSlots();
	RefreshInventory();
	RefreshHealth();
}

void UPlayerHUDWidget::SetInventoryVisible(bool bVisible)
{
	if (!InventoryPanel)
	{
		return;
	}

	// 인벤토리를 닫을 때 커서 아이템 자동 복귀
	if (!bVisible && CachedPlayer.IsValid())
	{
		if (UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent())
		{
			InventoryComp->ServerHandleRightClickInventory();
		}
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

void UPlayerHUDWidget::HandleAbilitySystemInitialized()
{
	BindHealthComponent(CachedPlayer.IsValid() ? CachedPlayer->GetHealthComponent() : nullptr);
	RefreshHealth();
}

void UPlayerHUDWidget::HandleHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealth();
}

void UPlayerHUDWidget::HandleMaxHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealth();
}

void UPlayerHUDWidget::BindHealthComponent(UBaseHealthComponent* HealthComponent)
{
	if (CachedHealthComponent == HealthComponent)
	{
		return;
	}

	UnbindHealthComponent();

	CachedHealthComponent = HealthComponent;

	if (CachedHealthComponent)
	{
		CachedHealthComponent->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
		CachedHealthComponent->OnMaxHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleMaxHealthChanged);
	}
}

void UPlayerHUDWidget::UnbindHealthComponent()
{
	if (!CachedHealthComponent)
	{
		return;
	}

	CachedHealthComponent->OnHealthChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
	CachedHealthComponent->OnMaxHealthChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleMaxHealthChanged);
	CachedHealthComponent = nullptr;
}

void UPlayerHUDWidget::RefreshHealth()
{
	if (!HealthBarWidget || !CachedHealthComponent)
	{
		return;
	}

	HealthBarWidget->SetHealthValues(
		CachedHealthComponent->GetHealth(),
		CachedHealthComponent->GetMaxHealth()
	);
}

void UPlayerHUDWidget::RefreshQuickSlots()
{
	if (!QuickSlotBox || !QuickSlotEntryClass)
	{
		return;
	}

	constexpr int32 QuickSlotCount = 3;

	if (QuickSlotEntries.Num() != QuickSlotCount || QuickSlotBox->GetChildrenCount() != QuickSlotCount)
	{
		QuickSlotBox->ClearChildren();
		QuickSlotEntries.Reset();

		for (int32 Index = 0; Index < QuickSlotCount; ++Index)
		{
			UQuickSlotEntryWidget* EntryWidget = CreateWidget<UQuickSlotEntryWidget>(this, QuickSlotEntryClass);
			if (!EntryWidget)
			{
				continue;
			}

			EntryWidget->SetVisibility(ESlateVisibility::Visible);
			QuickSlotBox->AddChild(EntryWidget);
			QuickSlotEntries.Add(EntryWidget);
		}
	}

	const FGameplayTag FallbackSlotTags[QuickSlotCount] =
	{
		Key_Item_1,
		Key_Item_2,
		Key_Item_3
	};

	for (int32 Index = 0; Index < QuickSlotEntries.Num(); ++Index)
	{
		UQuickSlotEntryWidget* EntryWidget = QuickSlotEntries[Index];
		if (!EntryWidget)
		{
			continue;
		}

		FGameplayTag SlotTag = FallbackSlotTags[Index];
		UTexture2D* Icon = nullptr;
		FText ItemName = FText::GetEmpty();
		bool bEquipped = false;

		if (CachedPlayer.IsValid() && CachedPlayer->ItemSlots.IsValidIndex(Index))
		{
			const FItemSlot& QuickSlot = CachedPlayer->ItemSlots[Index];
			SlotTag = QuickSlot.KeyTag.IsValid() ? QuickSlot.KeyTag : SlotTag;

			if (IsValid(QuickSlot.Item))
			{
				Icon = QuickSlot.Item->GetItemIcon();
				ItemName = QuickSlot.Item->GetItemNameText();
				bEquipped = (QuickSlot.Item == CachedPlayer->EquippedItem);
			}
		}

		EntryWidget->SetVisibility(ESlateVisibility::Visible);
		EntryWidget->SetupFromData(SlotTag, ItemName, Icon, bEquipped);
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

	const TArray<FInventorySlot>& Slots = InventoryComp->GetSlots();
	const int32 Columns = InventoryComp->GetInventoryColumns();
	const int32 SlotCount = InventoryComp->GetSlotCount();

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
				InventoryComp->GetMaterialName(InventorySlot.ItemTag),
				InventorySlot.Count,
				InventoryComp->GetMaterialIcon(InventorySlot.ItemTag),
				Index
			);
		}
		else
		{
			EntryWidget->SetupAsEmpty(Index);
		}

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

void UPlayerHUDWidget::RefreshCursorItemWidget()
{
	if (!InventoryCursorWidget || !CachedPlayer.IsValid())
	{
		return;
	}

	UInventoryComponent* InventoryComp = CachedPlayer->GetInventoryComponent();
	if (!InventoryComp)
	{
		InventoryCursorWidget->ClearCursorItem();
		return;
	}

	const FInventoryCursorItem& CursorItem = InventoryComp->GetCursorItem();

	if (!CursorItem.IsValid())
	{
		InventoryCursorWidget->ClearCursorItem();
		return;
	}

	InventoryCursorWidget->SetupCursorItem(
		InventoryComp->GetMaterialIcon(CursorItem.ItemTag),
		CursorItem.Count
	);
}

void UPlayerHUDWidget::UpdateCursorItemWidgetPosition()
{
	if (!InventoryCursorWidget || InventoryCursorWidget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;

	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);

	const FVector2D MousePosition(MouseX / ViewportScale, MouseY / ViewportScale);

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(InventoryCursorWidget->Slot))
	{
		// 마우스 포인터보다 살짝 오른쪽 아래에 보이게 오프셋
		CanvasSlot->SetPosition(MousePosition + FVector2D(12.0f, 12.0f));
	}
}
