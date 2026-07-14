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
class UBorder;
class UQuickSlotEntryWidget;
class UInventoryPanelWidget;
class UInventoryCursorWidget;
class UCanvasPanel;
class UInventoryCursorWidget;
class UHealthBarWidget;
class UBaseHealthComponent;
class UBowCrosshairWidget;
class UBowComponent;

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
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> QuickSlotBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> InventoryPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UInventoryPanelWidget> InventoryPanelWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UQuickSlotEntryWidget> QuickSlotEntryClass;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Dot", meta = (ClampMin = "0.0"))
	float CenterDotSize = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Dot")
	FLinearColor CenterDotColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "1.0"))
	float CrosshairReferenceShortSide = 1080.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float CrosshairMinScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float CrosshairMaxScale = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow Crosshair")
	TSubclassOf<UBowCrosshairWidget> BowCrosshairWidgetClass;

	UPROPERTY()
	TObjectPtr<UBowCrosshairWidget> BowCrosshairWidget;

	UPROPERTY()
	TObjectPtr<UBowComponent> BoundBowComponent;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void RefreshCursorItemWidget();
	void UpdateCursorItemWidgetPosition();
	void BindHealthComponent(UBaseHealthComponent* HealthComponent);
	void UnbindHealthComponent();
	void RefreshHealth();
	void CreateBowCrosshairWidget();
	void RefreshBowCrosshairBinding();
	void BindBowComponent(UBowComponent* BowComponent);
	void UnbindBowComponent();
	float GetCrosshairResponsiveScale(const FVector2D& LocalSize) const;

	void RefreshQuickSlots();

	void HandleInventoryChanged();
	void HandleItemSlotsChanged();
	void HandleAbilitySystemInitialized();

	UFUNCTION()
	void HandleHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	UFUNCTION()
	void HandleMaxHealthChanged(UBaseHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	UFUNCTION()
	void HandleBowAimStateChanged(bool bIsAiming);

	UFUNCTION()
	void HandleBowDrawAlphaChanged(float DrawAlpha);
};
