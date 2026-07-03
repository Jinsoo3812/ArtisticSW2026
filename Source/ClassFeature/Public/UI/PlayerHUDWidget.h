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
class UInventoryCursorWidget;
class UCanvasPanel;
class UInventoryCursorWidget;
class UHealthBarWidget;
class UBaseHealthComponent;

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

	UPROPERTY()
	TArray<TObjectPtr<UQuickSlotEntryWidget>> QuickSlotEntries;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> RootCanvasPanel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UInventoryCursorWidget> InventoryCursorWidgetClass;

	UPROPERTY()
	TObjectPtr<UInventoryCursorWidget> InventoryCursorWidget;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHealthBarWidget> HealthBarWidget;

	UPROPERTY()
	TObjectPtr<UBaseHealthComponent> CachedHealthComponent;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void RefreshCursorItemWidget();
	void UpdateCursorItemWidgetPosition();
	void BindHealthComponent(UBaseHealthComponent* HealthComponent);
	void UnbindHealthComponent();
	void RefreshHealth();

	void RefreshQuickSlots();
	void RefreshInventory();

	void HandleInventoryChanged();
	void HandleItemSlotsChanged();
	void HandleAbilitySystemInitialized();

	UFUNCTION()
	void HandleHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	UFUNCTION()
	void HandleMaxHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);
};
