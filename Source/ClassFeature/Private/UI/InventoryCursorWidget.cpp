// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InventoryCursorWidget.h"
#include "UI/ItemIconLibrary.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetLayoutLibrary.h"

void UInventoryCursorWidget::SetupCursorItem(UTexture2D* InIcon, int32 InCount, FGameplayTag InItemTag)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (ItemIconImage)
	{
		UItemIconLibrary::SetItemIcon(ItemIconImage, InIcon, InItemTag);
		ItemIconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	if (CountText)
	{
		CountText->SetText(FText::AsNumber(InCount));
		CountText->SetVisibility(InCount >= 1 ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UInventoryCursorWidget::ClearCursorItem()
{
	SetVisibility(ESlateVisibility::Collapsed);

	if (ItemIconImage)
	{
		ItemIconImage->SetBrushFromTexture(nullptr);
	}

	if (CountText)
	{
		CountText->SetText(FText::GetEmpty());
	}
}

void UInventoryCursorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!PlayerController->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	const FVector2D MousePosition(MouseX / ViewportScale, MouseY / ViewportScale);
	SetPositionInViewport(MousePosition + FVector2D(12.0f, 12.0f), false);
}
