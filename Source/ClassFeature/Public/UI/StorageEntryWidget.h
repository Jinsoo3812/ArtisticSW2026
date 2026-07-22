// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StorageEntryWidget.generated.h"

class AStorageChest;
class UBorder;
class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

UCLASS()
class CLASSFEATURE_API UStorageEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupFromData(const FText& InItemName, int32 InCount, UTexture2D* InIcon, int32 InSlotIndex, AStorageChest* InStorageChest);
	void SetupAsEmpty(int32 InSlotIndex, AStorageChest* InStorageChest);
	void SetupAsSearching(int32 InSlotIndex, AStorageChest* InStorageChest, UTexture2D* InSearchIcon);
	void SetupAsUnrevealed(int32 InSlotIndex, AStorageChest* InStorageChest, UTexture2D* InUnrevealedOverlayTexture);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION()
	void HandleSlotClicked();

	void BuildWidgetTree();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> SlotButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ItemIconImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SearchIconImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> UnrevealedOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> UnrevealedOverlayImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY()
	TObjectPtr<AStorageChest> StorageChest;

	int32 SlotIndex = INDEX_NONE;
	bool bCanInteract = true;
	bool bIsSearching = false;
	float SearchRotationAngle = 0.0f;
};
