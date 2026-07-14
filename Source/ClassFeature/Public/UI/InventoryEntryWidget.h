// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "InventoryEntryWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_TwoParams(FInventoryEntryHoverDelegate, int32, FGameplayTag);
DECLARE_DELEGATE_OneParam(FInventoryEntryUnhoverDelegate, int32);

/**
 * 
 */
UCLASS()
class CLASSFEATURE_API UInventoryEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex, FGameplayTag InItemTag, const FText& InRarityName);
	void SetupAsEmpty(int32 InSlotIndex);

	FInventoryEntryHoverDelegate OnEntryHovered;
	FInventoryEntryUnhoverDelegate OnEntryUnhovered;

protected:

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,	const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;;
	
	int32 SlotIndex = INDEX_NONE;
	FGameplayTag ItemTag;
};
