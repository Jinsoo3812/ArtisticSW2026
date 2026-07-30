// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/PlayerHUDWidget.h"
#include "UI/QuickSlotEntryWidget.h"
#include "UI/InventoryPanelWidget.h"
#include "BasePlayer.h"
#include "Inventory/InventoryComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/Border.h"
#include "UI/InventoryCursorWidget.h"
#include "UI/HealthBarWidget.h"
#include "UI/BowCrosshairWidget.h"
#include "UI/SkillQuickSlotWidget.h"
#include "UI/StorageWindowWidget.h"
#include "Storage/StorageChest.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/BaseHealthComponent.h"
#include "Item/Components/BowComponent.h"
#include "Item/Weapons/BowItem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetTree.h"
#include "Skills/PlayerSkillComponent.h"
#include "Cannon.h"
#include "Ship.h"
#include "GameFramework/PlayerController.h"

#include "BaseGameplayTags.h"

#include "BaseItem.h"

int32 UPlayerHUDWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 PaintedLayerId = Super::NativePaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float Scale = GetCrosshairResponsiveScale(LocalSize);
	const float ScaledDotSize = CenterDotSize * Scale;
	if (ScaledDotSize <= 0.0f)
	{
		return PaintedLayerId;
	}

	const FVector2D Center = LocalSize * 0.5f;
	const FVector2D DotPosition = Center - FVector2D(ScaledDotSize * 0.5f);
	// 화면 중앙에 점 그리기
	// TODO: 삼인칭 화면에 맞는 항상 떠있는 CrossHair 추가하기
	const FSlateRoundedBoxBrush DotBrush(
		CenterDotColor,
		ScaledDotSize * 0.5f,
		FVector2f(ScaledDotSize, ScaledDotSize));

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		PaintedLayerId + 1,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(ScaledDotSize, ScaledDotSize),
			FSlateLayoutTransform(FVector2f(DotPosition))),
		&DotBrush,
		ESlateDrawEffect::None,
		CenterDotColor);

	return PaintedLayerId + 1;
}


void UPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (InventoryPanel)
	{
		InventoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (StorageWindowWidget)
	{
		StorageWindowWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (InventoryCursorWidgetClass && !InventoryCursorWidget)
	{
		InventoryCursorWidget = CreateWidget<UInventoryCursorWidget>(GetOwningPlayer(), InventoryCursorWidgetClass);

		if (InventoryCursorWidget)
		{
			// The status window hides the HUD while it is open. Keep the held item
			// in the viewport so it remains visible above either inventory screen.
			InventoryCursorWidget->AddToViewport(999);
			InventoryCursorWidget->ClearCursorItem();
		}
	}

	CreateBowCrosshairWidget();
	RefreshBowCrosshairBinding();

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->OnPossessedPawnChanged.AddUniqueDynamic(
			this, &UPlayerHUDWidget::HandlePossessedPawnChanged);
		BoundPossessionController = PlayerController;
		BindSkillStateSource(PlayerController->GetPawn());
	}
}

void UPlayerHUDWidget::NativeDestruct()
{
	HideStorageWindow();
	if (InventoryCursorWidget)
	{
		InventoryCursorWidget->RemoveFromParent();
		InventoryCursorWidget = nullptr;
	}

	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.RemoveAll(this);
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	UnbindHealthComponent();
	UnbindBowComponent();
	UnbindSkillComponent();
	UnbindSkillStateSource();
	if (APlayerController* PlayerController = BoundPossessionController.Get())
	{
		PlayerController->OnPossessedPawnChanged.RemoveDynamic(
			this, &UPlayerHUDWidget::HandlePossessedPawnChanged);
	}
	BoundPossessionController.Reset();

	Super::NativeDestruct();
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshCursorItemWidget();
}

void UPlayerHUDWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.RemoveAll(this);
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);

		if (UInventoryComponent* OldInventory = CachedPlayer->GetInventoryComponent())
		{
			OldInventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	UnbindHealthComponent();
	UnbindSkillComponent();

	CachedPlayer = InPlayer;

	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.AddUObject(this, &UPlayerHUDWidget::HandleAbilitySystemInitialized);
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UPlayerHUDWidget::HandleItemSlotsChanged);
		CachedPlayer->OnQuickSlotsChanged.AddUObject(this, &UPlayerHUDWidget::HandleItemSlotsChanged);

		BindHealthComponent(CachedPlayer->GetHealthComponent());
		BindSkillComponent(CachedPlayer->GetPlayerSkillComponent());
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.AddUObject(this, &UPlayerHUDWidget::HandleInventoryChanged);
		}
	}

	RefreshQuickSlots();
	RefreshSkillQuickSlots();
	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->InitializeForPlayer(CachedPlayer.Get());
	}
	RefreshHealth();
	RefreshBowCrosshairBinding();
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
		if (InventoryPanelWidget)
		{
			InventoryPanelWidget->RefreshInventory();
		}
	}
}

bool UPlayerHUDWidget::IsInventoryVisible() const
{
	return InventoryPanel && InventoryPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

UStorageWindowWidget* UPlayerHUDWidget::ShowStorageWindow(
	AStorageChest* StorageChest,
	ABasePlayer* Player,
	TSubclassOf<UStorageWindowWidget> StorageWindowClass)
{
	if (!StorageChest)
	{
		return nullptr;
	}

	if (!StorageWindowWidget)
	{
		if (!RootCanvasPanel)
		{
			return nullptr;
		}

		if (!StorageWindowClass)
		{
			StorageWindowClass = UStorageWindowWidget::StaticClass();
		}

		StorageWindowWidget = CreateWidget<UStorageWindowWidget>(GetOwningPlayer(), StorageWindowClass);
		if (!StorageWindowWidget)
		{
			return nullptr;
		}

		bRuntimeStorageWindow = true;
		RootCanvasPanel->AddChild(StorageWindowWidget);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(StorageWindowWidget->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(1.0f, 0.0f));
			CanvasSlot->SetAlignment(FVector2D(1.0f, 0.0f));
			CanvasSlot->SetPosition(FVector2D(-RuntimeStorageWindowTopRightMargin.X, RuntimeStorageWindowTopRightMargin.Y));
			CanvasSlot->SetAutoSize(true);
			CanvasSlot->SetZOrder(20);
		}
	}

	StorageWindowWidget->InitializeStorage(StorageChest, Player);
	StorageWindowWidget->SetVisibility(ESlateVisibility::Visible);
	return StorageWindowWidget;
}

void UPlayerHUDWidget::HideStorageWindow()
{
	if (!StorageWindowWidget)
	{
		return;
	}

	if (bRuntimeStorageWindow)
	{
		StorageWindowWidget->RemoveFromParent();
		StorageWindowWidget = nullptr;
		bRuntimeStorageWindow = false;
		return;
	}

	StorageWindowWidget->SetVisibility(ESlateVisibility::Collapsed);
}

void UPlayerHUDWidget::HandleInventoryChanged()
{
	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->RefreshInventory();
	}
	RefreshQuickSlots();
	RefreshSkillQuickSlots();
	RefreshCursorItemWidget();
}

void UPlayerHUDWidget::HandleItemSlotsChanged()
{
	RefreshQuickSlots();
	RefreshBowCrosshairBinding();
}

void UPlayerHUDWidget::HandleAbilitySystemInitialized()
{
	BindHealthComponent(CachedPlayer.IsValid() ? CachedPlayer->GetHealthComponent() : nullptr);
	BindSkillComponent(CachedPlayer.IsValid() ? CachedPlayer->GetPlayerSkillComponent() : nullptr);
	RefreshHealth();
	RefreshSkillQuickSlots();
}

void UPlayerHUDWidget::RefreshSkillQuickSlots()
{
	SkillQuickSlotEntries.Reset();
	if (!WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);
	for (UWidget* Widget : AllWidgets)
	{
		if (USkillQuickSlotWidget* SkillSlot = Cast<USkillQuickSlotWidget>(Widget))
		{
			SkillQuickSlotEntries.Add(SkillSlot);
			SkillSlot->InitializeForPlayer(CachedPlayer.Get());
		}
	}
}

void UPlayerHUDWidget::BindSkillComponent(UPlayerSkillComponent* SkillComponent)
{
	if (BoundSkillComponent == SkillComponent)
	{
		return;
	}

	UnbindSkillComponent();
	BoundSkillComponent = SkillComponent;
	if (BoundSkillComponent)
	{
		BoundSkillComponent->OnSkillChanged.AddDynamic(this, &UPlayerHUDWidget::HandleSkillChanged);
	}
}

void UPlayerHUDWidget::UnbindSkillComponent()
{
	if (BoundSkillComponent)
	{
		BoundSkillComponent->OnSkillChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleSkillChanged);
		BoundSkillComponent = nullptr;
	}
}

void UPlayerHUDWidget::HandleSkillChanged(FGameplayTag SkillTag)
{
	RefreshSkillQuickSlots();
}

void UPlayerHUDWidget::BindSkillStateSource(APawn* ControlledPawn)
{
	UnbindSkillStateSource();

	if (ACannon* Cannon = Cast<ACannon>(ControlledPawn))
	{
		BoundSkillStateCannon = Cannon;
		Cannon->OnWaterBombModeChanged.AddUObject(
			this, &UPlayerHUDWidget::HandleSkillActiveStateChanged);
	}
	else if (AShip* Ship = Cast<AShip>(ControlledPawn))
	{
		BoundSkillStateShip = Ship;
		Ship->OnBombardmentTargetingChanged.AddUObject(
			this, &UPlayerHUDWidget::HandleSkillActiveStateChanged);
	}

	RefreshEquippedSkillBorders();
}

void UPlayerHUDWidget::UnbindSkillStateSource()
{
	if (ACannon* Cannon = BoundSkillStateCannon.Get())
	{
		Cannon->OnWaterBombModeChanged.RemoveAll(this);
	}
	if (AShip* Ship = BoundSkillStateShip.Get())
	{
		Ship->OnBombardmentTargetingChanged.RemoveAll(this);
	}

	BoundSkillStateCannon.Reset();
	BoundSkillStateShip.Reset();
}

void UPlayerHUDWidget::RefreshEquippedSkillBorders()
{
	APawn* ControlledPawn = GetOwningPlayerPawn();
	for (USkillQuickSlotWidget* SkillSlot : SkillQuickSlotEntries)
	{
		if (SkillSlot)
		{
			SkillSlot->RefreshEquippedState(ControlledPawn);
		}
	}
}

void UPlayerHUDWidget::HandlePossessedPawnChanged(APawn*, APawn* NewPawn)
{
	BindSkillStateSource(NewPawn);
}

void UPlayerHUDWidget::HandleSkillActiveStateChanged(bool)
{
	RefreshEquippedSkillBorders();
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

// BowCrossHair를 생성 (실제로 그리는 것은 BowCrossHairWidget.cpp에서 처리) 여기서는 그리는 준비 
void UPlayerHUDWidget::CreateBowCrosshairWidget()
{
	if (!RootCanvasPanel || !BowCrosshairWidgetClass || BowCrosshairWidget)
	{
		return;
	}

	BowCrosshairWidget = CreateWidget<UBowCrosshairWidget>(this, BowCrosshairWidgetClass);
	if (!BowCrosshairWidget)
	{
		return;
	}

	RootCanvasPanel->AddChild(BowCrosshairWidget);

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(BowCrosshairWidget->Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(0.0f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
		CanvasSlot->SetZOrder(100);
	}
}

// 현재 장착 아이템이 활인지 확인하고 bow 컴포넌트와 연결하고 해당 컴포넌트의 이벤트 구독
void UPlayerHUDWidget::RefreshBowCrosshairBinding()
{
	CreateBowCrosshairWidget();

	UBowComponent* NewBowComponent = nullptr;
	if (CachedPlayer.IsValid())
	{
		if (const ABowItem* BowItem = Cast<ABowItem>(CachedPlayer->EquippedItem))
		{
			NewBowComponent = BowItem->GetBowComponent();
		}
	}

	BindBowComponent(NewBowComponent);

	if (BowCrosshairWidget)
	{
		BowCrosshairWidget->SetBowEquipped(NewBowComponent != nullptr);
		BowCrosshairWidget->SetBowAiming(NewBowComponent ? NewBowComponent->IsAiming() : false);
		BowCrosshairWidget->SetDrawAlpha(NewBowComponent ? NewBowComponent->GetDrawAlpha() : 0.0f);
	}
}

// 활 컴포넌트의 이벤트와 HUD를 연결, 전달된 컴포넌트가 새 컴포넌트면 기존 컴포넌트를 Unbind 후 새 컴포넌트와 bind
void UPlayerHUDWidget::BindBowComponent(UBowComponent* BowComponent)
{
	if (BoundBowComponent == BowComponent)
	{
		return;
	}

	UnbindBowComponent();

	BoundBowComponent = BowComponent;
	if (BoundBowComponent)
	{
		BoundBowComponent->OnAimStateChanged.AddDynamic(this, &UPlayerHUDWidget::HandleBowAimStateChanged);
		BoundBowComponent->OnDrawAlphaChanged.AddDynamic(this, &UPlayerHUDWidget::HandleBowDrawAlphaChanged);
	}
}

// 연결된 컴포넌트의 브로드캐스트 구독 해제
void UPlayerHUDWidget::UnbindBowComponent()
{
	if (!BoundBowComponent)
	{
		return;
	}

	BoundBowComponent->OnAimStateChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleBowAimStateChanged);
	BoundBowComponent->OnDrawAlphaChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleBowDrawAlphaChanged);
	BoundBowComponent = nullptr;
}

//활의 조준 상태가 바뀌었을 때 호출
void UPlayerHUDWidget::HandleBowAimStateChanged(bool bIsAiming)
{
	if (BowCrosshairWidget)
	{
		BowCrosshairWidget->SetBowAiming(bIsAiming);
	}
}

// 활의 차징 정도가 바뀔 때 알려줌
void UPlayerHUDWidget::HandleBowDrawAlphaChanged(float DrawAlpha)
{
	if (BowCrosshairWidget)
	{
		BowCrosshairWidget->SetDrawAlpha(DrawAlpha);
	}
}

// Corss Hair의 스케일 조정
float UPlayerHUDWidget::GetCrosshairResponsiveScale(const FVector2D& LocalSize) const
{
	const float ShortSide = FMath::Min(LocalSize.X, LocalSize.Y);
	if (CrosshairReferenceShortSide <= 0.0f || ShortSide <= 0.0f)
	{
		return 1.0f;
	}

	return FMath::Clamp(ShortSide / CrosshairReferenceShortSide, CrosshairMinScale, CrosshairMaxScale);
}

void UPlayerHUDWidget::RefreshQuickSlots()
{
	constexpr int32 QuickSlotCount = 5;
	TArray<UQuickSlotEntryWidget*> EditorPlacedEntries =
	{
		WeaponQuickSlot1, WeaponQuickSlot2, ConsumableQuickSlot3, ConsumableQuickSlot4, ConsumableQuickSlot5
	};
	const bool bHasEditorPlacedEntries = EditorPlacedEntries.ContainsByPredicate([](const UQuickSlotEntryWidget* Entry)
	{
		return Entry != nullptr;
	});

	if (bHasEditorPlacedEntries)
	{
		QuickSlotEntries.Reset();
		for (UQuickSlotEntryWidget* Entry : EditorPlacedEntries)
		{
			QuickSlotEntries.Add(Entry);
		}
	}
	else if (QuickSlotBox && QuickSlotEntryClass &&
		(QuickSlotEntries.Num() != QuickSlotCount || QuickSlotBox->GetChildrenCount() != QuickSlotCount))
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
	else if (!QuickSlotBox || !QuickSlotEntryClass)
	{
		return;
	}

	const FGameplayTag FallbackSlotTags[QuickSlotCount] =
	{
		Key_Item_1,
		Key_Item_2,
		Key_Item_3,
		Key_Item_4,
		Key_Item_5
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
		int32 Count = 0;

		if (CachedPlayer.IsValid() && CachedPlayer->QuickSlots.IsValidIndex(Index))
		{
			const FQuickSlotReference& QuickSlot = CachedPlayer->QuickSlots[Index];
			SlotTag = QuickSlot.KeyTag.IsValid() ? QuickSlot.KeyTag : SlotTag;

			if (QuickSlot.ItemTag.IsValid())
			{
				if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
				{
					Icon = Inventory->GetMaterialIcon(QuickSlot.ItemTag);
					ItemName = Inventory->GetMaterialName(QuickSlot.ItemTag);
					Count = Inventory->GetMaterialCount(QuickSlot.ItemTag);
				}
				bEquipped = IsValid(CachedPlayer->EquippedItem) && CachedPlayer->EquippedItem->ItemTag == QuickSlot.ItemTag;
			}
		}

		EntryWidget->SetVisibility(ESlateVisibility::Visible);
		EntryWidget->ConfigureInteraction(Index, false);
		EntryWidget->SetupFromData(SlotTag, ItemName, Icon, bEquipped, Count);
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
