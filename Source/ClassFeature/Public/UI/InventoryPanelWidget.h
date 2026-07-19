// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inventory/InventoryComponent.h"
#include "InventoryPanelWidget.generated.h"

class ABasePlayer;
class UButton;
class UImage;
class UInventoryEntryWidget;
class UTextBlock;
class UUniformGridPanel;

UCLASS()
class CLASSFEATURE_API UInventoryPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void InitializeForPlayer(ABasePlayer* InPlayer);
	void RefreshInventory();
	void ClearItemInfo();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> InventoryGridPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClueTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumableTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> MaterialTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> WeaponTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemInfoNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemInfoDescriptionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemInfoCountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemInfoRarityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ItemInfoIconImage;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInventoryEntryWidget> InventoryEntryClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Tabs")
	FLinearColor ActiveTabColor = FLinearColor(0.0f, 0.75f, 0.8f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Tabs")
	FLinearColor InactiveTabColor = FLinearColor::White;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;

	void RefreshItemInfo(FGameplayTag ItemTag, int32 Count);
	void BindInventoryComponent(UInventoryComponent* InventoryComponent);
	void UnbindInventoryComponent();
	void RefreshTabButtonStyles();
	void ApplyTabButtonColor(UButton* Button, bool bIsActive);

	void HandleInventoryChanged();
	void HandleInventoryEntryHovered(int32 SlotIndex, FGameplayTag ItemTag);
	void HandleInventoryEntryUnhovered(int32 SlotIndex);
	void SetInventoryTab(EInventoryTab NewTab);

	UFUNCTION()
	void HandleClueTabClicked();

	UFUNCTION()
	void HandleConsumableTabClicked();

	UFUNCTION()
	void HandleMaterialTabClicked();

	UFUNCTION()
	void HandleWeaponTabClicked();

private:
	UPROPERTY()
	TObjectPtr<UInventoryComponent> BoundInventoryComponent;
};
