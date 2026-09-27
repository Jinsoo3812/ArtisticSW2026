// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "QuickSlotEntryWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class UTexture2D;

/**
 *
 */
UCLASS()
class CLASSFEATURE_API UQuickSlotEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromData(const FGameplayTag& InSlotTag, UTexture2D* InIcon, bool bEquipped, int32 InCount = 0);
	void ConfigureInteraction(int32 InQuickSlotIndex, bool bInInteractive);

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SlotText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> EquippedBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SlotFrameBorder;

	int32 QuickSlotIndex = INDEX_NONE;
	bool bInteractive = false;
	
};
