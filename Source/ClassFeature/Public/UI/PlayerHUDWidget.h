// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHUDWidget.generated.h"

/**
 * 
 */
class ABasePlayer;
class UHorizontalBox;
class UUniformGridPanel;
class UBorder;
class UQuickSlotEntryWidget;
class UInventoryEntryWidget;

UCLASS()
class CLASSFEATURE_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void InitializeForPlayer(ABasePlayer* InPlayer);

	void SetInventoryVisible(bool bVisible);
	bool IsInventoryVisible() const;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> QuickSlotBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> InventoryPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> InventoryGridPanel;;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UQuickSlotEntryWidget> QuickSlotEntryClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInventoryEntryWidget> InventoryEntryClass;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;

	void RefreshQuickSlots();
	void RefreshInventory();

	void HandleInventoryChanged();
	void HandleItemSlotsChanged();
};