#include "UI/StatusWindowWidget.h"

#include "BaseItem.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "BaseGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Inventory/InventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "UI/InventoryPanelWidget.h"
#include "UI/PlayerStatusPreviewStage.h"
#include "UI/QuickSlotEntryWidget.h"

namespace
{
	UCanvasPanel* FindStatusPreviewCanvas(UWidgetTree* WidgetTree)
	{
		if (!WidgetTree)
		{
			return nullptr;
		}

		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
		{
			return RootCanvas;
		}

		TArray<UWidget*> Widgets;
		WidgetTree->GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Widget))
			{
				return Canvas;
			}
		}

		return nullptr;
	}
}

void UStatusWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsurePlayerPreviewWidgets();
	BindPlayerAttributes();
}

void UStatusWindowWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (!Image_PlayerPreview)
	{
		return;
	}

	if (IsDesignTime())
	{
		if (UTexture2D* PlaceholderTexture = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")))
		{
			Image_PlayerPreview->SetBrushFromTexture(PlaceholderTexture, false);
		}
		if (UOverlaySlot* ImageSlot = Cast<UOverlaySlot>(Image_PlayerPreview->Slot))
		{
			ImageSlot->SetHorizontalAlignment(HAlign_Fill);
			ImageSlot->SetVerticalAlignment(VAlign_Fill);
			ImageSlot->SetPadding(FMargin(0.0f));
		}
		if (UPanelWidget* PreviewOverlay = Image_PlayerPreview->GetParent())
		{
			if (USizeBoxSlot* OverlaySlot = Cast<USizeBoxSlot>(PreviewOverlay->Slot))
			{
				OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
				OverlaySlot->SetVerticalAlignment(VAlign_Fill);
				OverlaySlot->SetPadding(FMargin(0.0f));
			}
		}
		Image_PlayerPreview->SetColorAndOpacity(FLinearColor(0.05f, 0.45f, 1.0f, 0.35f));
		Image_PlayerPreview->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

int32 UStatusWindowWidget::NativePaint(
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
	int32 HighestLayerId = PaintedLayerId;

	const UWidget* LevelProgressAnchor = SizeBox_LevelProgressRing
		? static_cast<const UWidget*>(SizeBox_LevelProgressRing.Get())
		: static_cast<const UWidget*>(LevelText.Get());
	if (bShowLevelProgressRing && LevelProgressAnchor)
	{
		const FGeometry& AnchorGeometry = LevelProgressAnchor->GetPaintSpaceGeometry();
		const FVector2D AnchorSize = AnchorGeometry.GetLocalSize();
		if (AnchorSize.X > 0.0f && AnchorSize.Y > 0.0f)
		{
			const FVector2D LevelCenter = AllottedGeometry.AbsoluteToLocal(
				AnchorGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
			const float Thickness = FMath::Max(1.0f, LevelProgressRingThickness);
			const float Radius = SizeBox_LevelProgressRing
				? FMath::Max(1.0f, FMath::Min(AnchorSize.X, AnchorSize.Y) * 0.5f - Thickness * 0.5f)
				: FMath::Max(
					LevelProgressRingRadius,
					FMath::Max(AnchorSize.X, AnchorSize.Y) * 0.5f + Thickness);
			const int32 SegmentCount = FMath::Clamp(LevelProgressRingSegments, 12, 256);
			const float StartAngle = -UE_HALF_PI;

			TArray<FVector2D> BackgroundPoints;
			BackgroundPoints.Reserve(SegmentCount + 1);
			for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
			{
				const float Angle = StartAngle
					+ UE_TWO_PI * static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
				BackgroundPoints.Add(LevelCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				HighestLayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				BackgroundPoints,
				ESlateDrawEffect::None,
				LevelProgressRingBackgroundColor * InWidgetStyle.GetColorAndOpacityTint(),
				true,
				Thickness);
			HighestLayerId++;

			const float ClampedProgress = FMath::Clamp(LevelProgress, 0.0f, 1.0f);
			if (ClampedProgress > 0.0f)
			{
				const int32 ProgressSegmentCount = FMath::Max(
					1,
					FMath::CeilToInt(static_cast<float>(SegmentCount) * ClampedProgress));
				TArray<FVector2D> ProgressPoints;
				ProgressPoints.Reserve(ProgressSegmentCount + 1);
				for (int32 SegmentIndex = 0; SegmentIndex <= ProgressSegmentCount; ++SegmentIndex)
				{
					const float ProgressAlpha = ClampedProgress
						* static_cast<float>(SegmentIndex) / static_cast<float>(ProgressSegmentCount);
					const float Angle = StartAngle + UE_TWO_PI * ProgressAlpha;
					ProgressPoints.Add(LevelCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}

				FSlateDrawElement::MakeLines(
					OutDrawElements,
					HighestLayerId + 1,
					AllottedGeometry.ToPaintGeometry(),
					ProgressPoints,
					ESlateDrawEffect::None,
					LevelProgressRingFillColor * InWidgetStyle.GetColorAndOpacityTint(),
					true,
					Thickness);
				HighestLayerId++;
			}
		}
	}

	if (!PlayerPreviewMaterialInstance || !IsValid(PlayerPreviewStage))
	{
		return HighestLayerId;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return HighestLayerId;
	}

	FVector2D ClampedPreviewSize(
		FMath::Max(1.0f, PlayerPreviewSize.X),
		FMath::Max(1.0f, PlayerPreviewSize.Y));
	FVector2D PreviewPosition =
		(LocalSize - ClampedPreviewSize) * 0.5f + PlayerPreviewPositionOffset;

	if (SizeBox_PlayerPreview)
	{
		const FGeometry& PreviewGeometry = SizeBox_PlayerPreview->GetCachedGeometry();
		const FVector2D GeometrySize = PreviewGeometry.GetLocalSize();
		if (GeometrySize.X > 1.0f && GeometrySize.Y > 1.0f)
		{
			PreviewPosition = AllottedGeometry.AbsoluteToLocal(
				PreviewGeometry.LocalToAbsolute(FVector2D::ZeroVector));
			const FVector2D PreviewBottomRight = AllottedGeometry.AbsoluteToLocal(
				PreviewGeometry.LocalToAbsolute(GeometrySize));
			ClampedPreviewSize = PreviewBottomRight - PreviewPosition;
		}
	}
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		HighestLayerId + 1,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(ClampedPreviewSize),
			FSlateLayoutTransform(FVector2f(PreviewPosition))),
		&PlayerPreviewPaintBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	return HighestLayerId + 1;
}

FReply UStatusWindowWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer()))
		{
			PlayerController->ToggleStatus();
		}
		return FReply::Handled();
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UStatusWindowWidget::InitializeForPlayer(ABasePlayer* InPlayer)
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
	UnbindPlayerAttributes();

	CachedPlayer = InPlayer;
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnAbilitySystemInitialized.AddUObject(this, &UStatusWindowWidget::BindPlayerAttributes);
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UStatusWindowWidget::RefreshQuickSlots);
		CachedPlayer->OnQuickSlotsChanged.AddUObject(this, &UStatusWindowWidget::RefreshQuickSlots);
		if (UInventoryComponent* Inventory = CachedPlayer->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.AddUObject(this, &UStatusWindowWidget::HandleInventoryChanged);
		}
	}
	BindPlayerAttributes();

	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->InitializeForPlayer(CachedPlayer.Get());
	}
	RefreshQuickSlots();
	if (PlayerPreviewStage)
	{
		PlayerPreviewStage->SetSourcePlayer(CachedPlayer.Get());
	}
}

void UStatusWindowWidget::SetStatusVisible(bool bVisible)
{
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
		EnsurePlayerPreviewWidgets();
		SpawnPlayerPreview();
		if (InventoryPanelWidget)
		{
			InventoryPanelWidget->RefreshInventory();
		}
		RefreshQuickSlots();
	}
	else if (PlayerPreviewStage)
	{
		PlayerPreviewStage->SetPreviewEnabled(false);
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

void UStatusWindowWidget::SetMaxHealthValue(float MaxHealth)
{
	if (MaxHealthText)
	{
		MaxHealthText->SetText(FText::AsNumber(MaxHealth));
	}
}

void UStatusWindowWidget::SetAttackPowerValue(float AttackPower)
{
	if (AttackPowerText)
	{
		AttackPowerText->SetText(FText::AsNumber(AttackPower));
	}
}

void UStatusWindowWidget::SetExperienceValues(float CurrentExperience, float RequiredExperience)
{
	LevelProgress = RequiredExperience > 0.0f
		? FMath::Clamp(CurrentExperience / RequiredExperience, 0.0f, 1.0f)
		: 0.0f;

	if (ExperienceText)
	{
		ExperienceText->SetText(FText::Format(NSLOCTEXT("Status", "ExperienceFormat", "{0} / {1}"),
			FText::AsNumber(CurrentExperience), FText::AsNumber(RequiredExperience)));
	}
	if (ExperienceProgressBar)
	{
		ExperienceProgressBar->SetPercent(LevelProgress);
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
		UTexture2D* Icon = nullptr;
		int32 Count = 0;
		bool bEquipped = false;
		if (CachedPlayer->QuickSlots.IsValidIndex(Index))
		{
			const FQuickSlotReference& QuickSlot = CachedPlayer->QuickSlots[Index];
			SlotKeyTag = QuickSlot.KeyTag.IsValid() ? QuickSlot.KeyTag : SlotKeyTag;
			if (Inventory && QuickSlot.ItemTag.IsValid())
			{
				Icon = Inventory->GetMaterialIcon(QuickSlot.ItemTag);
				Count = Inventory->GetMaterialCount(QuickSlot.ItemTag);
				bEquipped = IsValid(CachedPlayer->EquippedItem) && CachedPlayer->EquippedItem->ItemTag == QuickSlot.ItemTag;
			}
		}

		Entry->ConfigureInteraction(Index, true);
		Entry->SetupFromData(SlotKeyTag, Icon, bEquipped, Count);
	}
	RefreshPlayerPreview();
}

void UStatusWindowWidget::HandleInventoryChanged()
{
	if (InventoryPanelWidget)
	{
		InventoryPanelWidget->RefreshInventory();
	}
	RefreshQuickSlots();
}

void UStatusWindowWidget::BindPlayerAttributes()
{
	UnbindPlayerAttributes();

	UAbilitySystemComponent* AbilitySystemComponent = CachedPlayer.IsValid()
		? CachedPlayer->GetAbilitySystemComponent()
		: nullptr;
	if (!AbilitySystemComponent)
	{
		return;
	}

	BoundAbilitySystemComponent = AbilitySystemComponent;
	MaxHealthChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UStatusWindowWidget::HandleMaxHealthChanged);
	AttackPowerChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackPowerAttribute())
		.AddUObject(this, &UStatusWindowWidget::HandleAttackPowerChanged);
	AttackSpeedChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute())
		.AddUObject(this, &UStatusWindowWidget::HandleAttackSpeedChanged);

	SetMaxHealthValue(AbilitySystemComponent->GetNumericAttribute(
		UBaseAttributeSet::GetMaxHealthAttribute()));
	SetAttackPowerValue(AbilitySystemComponent->GetNumericAttribute(
		UBaseAttributeSet::GetAttackPowerAttribute()));
	SetAttackSpeedValue(AbilitySystemComponent->GetNumericAttribute(
		UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()));
}

void UStatusWindowWidget::UnbindPlayerAttributes()
{
	if (BoundAbilitySystemComponent.IsValid())
	{
		if (MaxHealthChangedDelegateHandle.IsValid())
		{
			BoundAbilitySystemComponent
				->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
				.Remove(MaxHealthChangedDelegateHandle);
		}
		if (AttackPowerChangedDelegateHandle.IsValid())
		{
			BoundAbilitySystemComponent
				->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackPowerAttribute())
				.Remove(AttackPowerChangedDelegateHandle);
		}
		if (AttackSpeedChangedDelegateHandle.IsValid())
		{
			BoundAbilitySystemComponent
				->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute())
				.Remove(AttackSpeedChangedDelegateHandle);
		}
	}

	MaxHealthChangedDelegateHandle.Reset();
	AttackPowerChangedDelegateHandle.Reset();
	AttackSpeedChangedDelegateHandle.Reset();
	BoundAbilitySystemComponent.Reset();
}

void UStatusWindowWidget::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	SetMaxHealthValue(Data.NewValue);
}

void UStatusWindowWidget::HandleAttackPowerChanged(const FOnAttributeChangeData& Data)
{
	SetAttackPowerValue(Data.NewValue);
}

void UStatusWindowWidget::HandleAttackSpeedChanged(const FOnAttributeChangeData& Data)
{
	SetAttackSpeedValue(Data.NewValue);
}

void UStatusWindowWidget::NativeDestruct()
{
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
	UnbindPlayerAttributes();
	DestroyPlayerPreview();
	Super::NativeDestruct();
}

void UStatusWindowWidget::EnsurePlayerPreviewWidgets()
{
	if (Image_PlayerPreview)
	{
		if (!PlayerPreviewMaterialInstance)
		{
			UMaterialInterface* PreviewMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/M_ShipPreviewOverlay.M_ShipPreviewOverlay"));
			if (PreviewMaterial)
			{
				Image_PlayerPreview->SetBrushFromMaterial(PreviewMaterial);
				PlayerPreviewMaterialInstance = Image_PlayerPreview->GetDynamicMaterial();
				PlayerPreviewPaintBrush.SetResourceObject(PlayerPreviewMaterialInstance);
				PlayerPreviewPaintBrush.SetImageSize(FVector2D(520.0f, 720.0f));
			}
		}
		Image_PlayerPreview->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = FindStatusPreviewCanvas(WidgetTree);
	if (!RootCanvas)
	{
		UE_LOG(LogTemp, Error, TEXT("[StatusPreview] WBP_StatusWindow has no CanvasPanel for the player preview"));
		return;
	}

	// Runtime compatibility for the current WBP asset. These are the component and
	// variable names that should also be used if the hierarchy is later authored in Designer.
	SizeBox_PlayerPreview = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		TEXT("SizeBox_PlayerPreview"));
	UOverlay* Overlay_PlayerPreview = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(),
		TEXT("Overlay_PlayerPreview"));
	Image_PlayerPreview = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(),
		TEXT("Image_PlayerPreview"));
	if (!SizeBox_PlayerPreview || !Overlay_PlayerPreview || !Image_PlayerPreview)
	{
		return;
	}

	SizeBox_PlayerPreview->SetWidthOverride(520.f);
	SizeBox_PlayerPreview->SetHeightOverride(720.f);
	UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(SizeBox_PlayerPreview);
	CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CanvasSlot->SetPosition(FVector2D::ZeroVector);
	CanvasSlot->SetSize(FVector2D(520.f, 720.f));
	CanvasSlot->SetZOrder(100);
	SizeBox_PlayerPreview->AddChild(Overlay_PlayerPreview);
	Overlay_PlayerPreview->AddChild(Image_PlayerPreview);
	SizeBox_PlayerPreview->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Overlay_PlayerPreview->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Image_PlayerPreview->SetVisibility(ESlateVisibility::HitTestInvisible);
	Image_PlayerPreview->SetColorAndOpacity(FLinearColor::White);

	UMaterialInterface* PreviewMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/M_ShipPreviewOverlay.M_ShipPreviewOverlay"));
	if (PreviewMaterial)
	{
		Image_PlayerPreview->SetBrushFromMaterial(PreviewMaterial);
		PlayerPreviewMaterialInstance = Image_PlayerPreview->GetDynamicMaterial();
		PlayerPreviewPaintBrush.SetResourceObject(PlayerPreviewMaterialInstance);
		PlayerPreviewPaintBrush.SetImageSize(FVector2D(520.0f, 720.0f));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[StatusPreview] Transparent preview UI material failed to load"));
	}
	Image_PlayerPreview->SetVisibility(ESlateVisibility::Hidden);

	UE_LOG(LogTemp, Display, TEXT("[StatusPreview] Runtime widgets added to canvas %s"), *GetNameSafe(RootCanvas));
}

void UStatusWindowWidget::SpawnPlayerPreview()
{
	if (PlayerPreviewStage || !GetWorld() || !CachedPlayer.IsValid())
	{
		if (PlayerPreviewStage)
		{
			PlayerPreviewStage->SetPreviewEnabled(true);
		}
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwningPlayer();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PlayerPreviewStage = GetWorld()->SpawnActor<APlayerStatusPreviewStage>(
		APlayerStatusPreviewStage::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!PlayerPreviewStage)
	{
		UE_LOG(LogTemp, Error, TEXT("[StatusPreview] Preview stage failed to spawn"));
		return;
	}

	PlayerPreviewStage->SetSourcePlayer(CachedPlayer.Get());
	if (UTextureRenderTarget2D* RenderTarget = PlayerPreviewStage->GetRenderTarget())
	{
		if (PlayerPreviewMaterialInstance)
		{
			PlayerPreviewMaterialInstance->SetTextureParameterValue(TEXT("ShipPreviewTexture"), RenderTarget);
			PlayerPreviewPaintBrush.SetResourceObject(PlayerPreviewMaterialInstance);
			UE_LOG(LogTemp, Display, TEXT("[StatusPreview] Render target bound to Image_PlayerPreview"));
		}
		else if (Image_PlayerPreview)
		{
			FSlateBrush Brush = Image_PlayerPreview->GetBrush();
			Brush.SetResourceObject(RenderTarget);
			Brush.SetImageSize(FVector2D(RenderTarget->SizeX, RenderTarget->SizeY));
			Image_PlayerPreview->SetBrush(Brush);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[StatusPreview] Preview stage returned no render target"));
	}
	PlayerPreviewStage->SetPreviewEnabled(true);
}

void UStatusWindowWidget::DestroyPlayerPreview()
{
	if (IsValid(PlayerPreviewStage))
	{
		PlayerPreviewStage->Destroy();
	}
	PlayerPreviewStage = nullptr;
	PlayerPreviewMaterialInstance = nullptr;
}

void UStatusWindowWidget::RefreshPlayerPreview()
{
	if (PlayerPreviewStage && IsStatusVisible())
	{
		PlayerPreviewStage->RefreshFromPlayer();
	}
}
