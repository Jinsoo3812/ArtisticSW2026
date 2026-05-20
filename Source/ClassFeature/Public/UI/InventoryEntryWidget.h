// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "InventoryEntryWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;


/**
 * 
 */
UCLASS()
class CLASSFEATURE_API UInventoryEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex);
	void SetupAsEmpty(int32 InSlotIndex);

protected:

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,	const FPointerEvent& InMouseEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;;
	
	int32 SlotIndex = INDEX_NONE;
};
