// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/QuickSlotEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/Texture2D.h"
#include "BasePlayer.h"

void UQuickSlotEntryWidget::SetupFromData(const FGameplayTag& InSlotTag, const FText& InItemName, UTexture2D* InIcon, bool bEquipped, int32 InCount)
{
	// 퀵슬롯 칸 자체는 아이템 유무와 관계없이 항상 표시합니다.
	SetVisibility(ESlateVisibility::Visible);

	if (SlotFrameBorder)
	{
		SlotFrameBorder->SetVisibility(ESlateVisibility::Visible);
	}

	if (SlotText)
	{
		SlotText->SetVisibility(ESlateVisibility::Visible);

		FString SlotString = InSlotTag.ToString();
		FString Left, Right;
		if (SlotString.Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			SlotText->SetText(FText::FromString(Right));
		}
		else
		{
			SlotText->SetText(FText::FromString(SlotString));
		}
	}

	if (ItemNameText)
	{
		ItemNameText->SetVisibility(ESlateVisibility::Visible);
		ItemNameText->SetText(InItemName);
	}

	if (CountText)
	{
		CountText->SetText(InCount > 1 ? FText::AsNumber(InCount) : FText::GetEmpty());
		CountText->SetVisibility(InCount > 1 ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	if (ItemIconImage)
	{
		ItemIconImage->SetVisibility(ESlateVisibility::Visible);

		FSlateBrush IconBrush = ItemIconImage->GetBrush();
		IconBrush.SetImageSize(FVector2D(64.f, 64.f));
		ItemIconImage->SetBrush(IconBrush);

		if (InIcon)
		{
			ItemIconImage->SetBrushFromTexture(InIcon, true);
			ItemIconImage->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			ItemIconImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.f));
		}
	}

	if (EquippedBorder)
	{
		EquippedBorder->SetVisibility(bEquipped ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UQuickSlotEntryWidget::ConfigureInteraction(int32 InQuickSlotIndex, bool bInInteractive)
{
	QuickSlotIndex = InQuickSlotIndex;
	bInteractive = bInInteractive;
}

FReply UQuickSlotEntryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bInteractive || QuickSlotIndex == INDEX_NONE)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetOwningPlayerPawn());
	if (!Player)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Player->AssignQuickSlotFromInventory(QuickSlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Player->ClearQuickSlot(QuickSlotIndex);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}
