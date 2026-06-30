// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/StorageEntryWidget.h"
#include "BasePlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UStorageEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildWidgetTree();
}

void UStorageEntryWidget::SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex, AStorageChest* InStorageChest)
{
	BuildWidgetTree();

	SlotIndex = InSlotIndex;
	StorageChest = InStorageChest;

	SetToolTipText(InItemName);

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(InIcon, true);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (CountText)
	{
		CountText->SetText(FText::AsNumber(InCount));
		CountText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UStorageEntryWidget::SetupAsEmpty(int32 InSlotIndex, AStorageChest* InStorageChest)
{
	BuildWidgetTree();

	SlotIndex = InSlotIndex;
	StorageChest = InStorageChest;

	SetToolTipText(FText::GetEmpty());

	if (ItemIconImage)
	{
		ItemIconImage->SetVisibility(ESlateVisibility::Hidden);
	}

	if (CountText)
	{
		CountText->SetText(FText::GetEmpty());
		CountText->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UStorageEntryWidget::HandleSlotClicked()
{
	if (SlotIndex == INDEX_NONE || !StorageChest)
	{
		return;
	}

	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer());
	if (!PlayerController)
	{
		return;
	}

	PlayerController->ServerTransferStorageSlot(StorageChest, SlotIndex);
}

FReply UStorageEntryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (SlotIndex == INDEX_NONE || !StorageChest)
	{
		return FReply::Unhandled();
	}

	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer());
	if (!PlayerController)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (InMouseEvent.IsShiftDown())
		{
			PlayerController->ServerQuickMoveStorageSlotToInventory(StorageChest, SlotIndex);
			return FReply::Handled();
		}

		PlayerController->ServerHandleStorageLeftClick(StorageChest, SlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		PlayerController->ServerQuickMoveStorageSlotToInventory(StorageChest, SlotIndex);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void UStorageEntryWidget::BuildWidgetTree()
{
	if (SlotButton || !WidgetTree)
	{
		return;
	}

	USizeBox* RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(72.0f);
	RootSizeBox->SetHeightOverride(72.0f);

	UBorder* BackgroundBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(2.0f));
	BackgroundBorder->SetBrushColor(FLinearColor(0.03f, 0.035f, 0.04f, 0.92f));

	SlotButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SlotButton"));
	SlotButton->SetVisibility(ESlateVisibility::HitTestInvisible);

	UOverlay* SlotOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SlotOverlay"));

	ItemIconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ItemIconImage"));
	if (UOverlaySlot* IconSlot = SlotOverlay->AddChildToOverlay(ItemIconImage))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Fill);
		IconSlot->SetVerticalAlignment(VAlign_Fill);
	}
	ItemIconImage->SetVisibility(ESlateVisibility::Hidden);

	CountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CountText"));
	CountText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	CountText->SetJustification(ETextJustify::Right);
	CountText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	CountText->SetShadowColorAndOpacity(FLinearColor::Black);
	if (UOverlaySlot* CountSlot = SlotOverlay->AddChildToOverlay(CountText))
	{
		CountSlot->SetHorizontalAlignment(HAlign_Right);
		CountSlot->SetVerticalAlignment(VAlign_Bottom);
		CountSlot->SetPadding(FMargin(4.0f));
	}
	CountText->SetVisibility(ESlateVisibility::Hidden);

	SlotButton->AddChild(SlotOverlay);
	BackgroundBorder->SetContent(SlotButton);
	RootSizeBox->SetContent(BackgroundBorder);

	WidgetTree->RootWidget = RootSizeBox;
}
