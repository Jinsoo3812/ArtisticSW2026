// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
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
class AStorageChest;
class UStorageWindowWidget;
class UPlayerSkillComponent;
class USkillQuickSlotWidget;
class APlayerController;
class APawn;
class ACannon;
class AShip;
struct FOnAttributeChangeData;

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

	UStorageWindowWidget* ShowStorageWindow(
		AStorageChest* StorageChest,
		ABasePlayer* Player,
		TSubclassOf<UStorageWindowWidget> StorageWindowClass);
	void HideStorageWindow();

protected:
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> QuickSlotBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UQuickSlotEntryWidget> WeaponQuickSlot1;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UQuickSlotEntryWidget> WeaponQuickSlot2;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UQuickSlotEntryWidget> ConsumableQuickSlot3;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UQuickSlotEntryWidget> ConsumableQuickSlot4;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UQuickSlotEntryWidget> ConsumableQuickSlot5;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> InventoryPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UInventoryPanelWidget> InventoryPanelWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UQuickSlotEntryWidget> QuickSlotEntryClass;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;

	UPROPERTY()
	TArray<TObjectPtr<UQuickSlotEntryWidget>> QuickSlotEntries;

	/** Designer-placed USkillQuickSlotWidget children are discovered automatically. */
	UPROPERTY()
	TArray<TObjectPtr<USkillQuickSlotWidget>> SkillQuickSlotEntries;

	UPROPERTY()
	TObjectPtr<UPlayerSkillComponent> BoundSkillComponent;

	TWeakObjectPtr<APlayerController> BoundPossessionController;
	TWeakObjectPtr<ACannon> BoundSkillStateCannon;
	TWeakObjectPtr<AShip> BoundSkillStateShip;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> RootCanvasPanel;

	// When this widget is placed in the HUD designer with this exact name,
	// its Canvas Slot controls the chest window position.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UStorageWindowWidget> StorageWindowWidget;

	// Fallback placement used only when the HUD blueprint has no designer-placed storage window.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage")
	FVector2D RuntimeStorageWindowTopRightMargin = FVector2D(60.0f, 140.0f);

	bool bRuntimeStorageWindow = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UInventoryCursorWidget> InventoryCursorWidgetClass;

	UPROPERTY()
	TObjectPtr<UInventoryCursorWidget> InventoryCursorWidget;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHealthBarWidget> HealthBarWidget;

	UPROPERTY()
	TObjectPtr<UBaseHealthComponent> CachedHealthComponent;

	TWeakObjectPtr<AShip> CachedShipHealthSource;
	FDelegateHandle ShipHealthChangedDelegateHandle;
	FDelegateHandle ShipMaxHealthChangedDelegateHandle;

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
	void RefreshShipHealthSource(APawn* ControlledPawn);
	void BindShipHealthSource(AShip* Ship);
	void UnbindShipHealthSource();
	void RefreshShipHealth();
	void HandleShipHealthChanged(const FOnAttributeChangeData& Data);
	void HandleShipMaxHealthChanged(const FOnAttributeChangeData& Data);
	void CreateBowCrosshairWidget();
	void RefreshBowCrosshairBinding();
	void BindBowComponent(UBowComponent* BowComponent);
	void UnbindBowComponent();
	float GetCrosshairResponsiveScale(const FVector2D& LocalSize) const;

	void RefreshQuickSlots();
	void RefreshSkillQuickSlots();
	void BindSkillComponent(UPlayerSkillComponent* SkillComponent);
	void UnbindSkillComponent();
	void BindSkillStateSource(APawn* ControlledPawn);
	void UnbindSkillStateSource();
	void RefreshEquippedSkillBorders();

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

	UFUNCTION()
	void HandleSkillChanged(FGameplayTag SkillTag);

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void HandleSkillActiveStateChanged(bool bIsActive);
};
