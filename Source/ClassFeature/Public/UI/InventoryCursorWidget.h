// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryCursorWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

/**
 * 
 */

UCLASS()
class CLASSFEATURE_API UInventoryCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupCursorItem(UTexture2D* InIcon, int32 InCount);
	void ClearCursorItem();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;
};
