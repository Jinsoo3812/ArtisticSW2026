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
	void SetupFromData(const FGameplayTag& InSlotTag, const FText& InItemName, UTexture2D* InIcon, bool bEquipped);

protected:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SlotText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> EquippedBorder;
};
