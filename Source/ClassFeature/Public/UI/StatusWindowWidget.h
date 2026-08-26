#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "StatusWindowWidget.generated.h"

class ABasePlayer;
class APlayerStatusPreviewStage;
class UAbilitySystemComponent;
class UImage;
class UInventoryPanelWidget;
class UMaterialInstanceDynamic;
class UProgressBar;
class UQuickSlotEntryWidget;
class USizeBox;
class UTextBlock;
struct FOnAttributeChangeData;

UCLASS()
class CLASSFEATURE_API UStatusWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeDestruct() override;
	void InitializeForPlayer(ABasePlayer* InPlayer);
	void SetStatusVisible(bool bVisible);
	bool IsStatusVisible() const;

	UFUNCTION(BlueprintCallable, Category = "Status|Data")
	void SetLevelValue(int32 Level);

	UFUNCTION(BlueprintCallable, Category = "Status|Data")
	void SetAttackSpeedValue(float AttackSpeed);

	UFUNCTION(BlueprintCallable, Category = "Status|Data")
	void SetMaxHealthValue(float MaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Status|Data")
	void SetAttackPowerValue(float AttackPower);

	UFUNCTION(BlueprintCallable, Category = "Status|Data")
	void SetExperienceValues(float CurrentExperience, float RequiredExperience);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInventoryPanelWidget> InventoryPanelWidget;

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

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AttackSpeedText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MaxHealthText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AttackPowerText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExperienceText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ExperienceProgressBar;

	/**
	 * WBP component: SizeBox named SizeBox_LevelProgressRing.
	 * Its Designer position and size define the circular ring bounds. Place LevelText
	 * inside this SizeBox and center its SizeBox slot to share the exact same center.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SizeBox_LevelProgressRing;

	/** Draws the current-level experience progress clockwise around LevelText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress")
	bool bShowLevelProgressRing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress", meta = (ClampMin = "1.0"))
	float LevelProgressRingRadius = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress", meta = (ClampMin = "1.0"))
	float LevelProgressRingThickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress", meta = (ClampMin = "12", ClampMax = "256"))
	int32 LevelProgressRingSegments = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress")
	FLinearColor LevelProgressRingBackgroundColor = FLinearColor(0.08f, 0.08f, 0.08f, 0.65f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Level Progress")
	FLinearColor LevelProgressRingFillColor = FLinearColor(0.95f, 0.72f, 0.18f, 1.0f);

	/** WBP component: Image named Image_PlayerPreview. Created at runtime for legacy assets. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_PlayerPreview;

	/** WBP component: SizeBox named SizeBox_PlayerPreview. Move and resize this in Designer. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SizeBox_PlayerPreview;

	UPROPERTY(Transient)
	TObjectPtr<APlayerStatusPreviewStage> PlayerPreviewStage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlayerPreviewMaterialInstance;

	FSlateBrush PlayerPreviewPaintBrush;
	float LevelProgress = 0.0f;

	/** WBP_StatusWindow Class Defaults: offset from the center of the status window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Player Preview", meta = (DisplayName = "Preview Position Offset"))
	FVector2D PlayerPreviewPositionOffset = FVector2D::ZeroVector;

	/** WBP_StatusWindow Class Defaults: rendered player preview size in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Player Preview", meta = (DisplayName = "Preview Size", ClampMin = "1.0"))
	FVector2D PlayerPreviewSize = FVector2D(520.0f, 720.0f);

	TWeakObjectPtr<ABasePlayer> CachedPlayer;
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;
	FDelegateHandle MaxHealthChangedDelegateHandle;
	FDelegateHandle AttackPowerChangedDelegateHandle;
	FDelegateHandle AttackSpeedChangedDelegateHandle;

	void RefreshQuickSlots();
	void HandleInventoryChanged();
	void BindPlayerAttributes();
	void UnbindPlayerAttributes();
	void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);
	void HandleAttackPowerChanged(const FOnAttributeChangeData& Data);
	void HandleAttackSpeedChanged(const FOnAttributeChangeData& Data);
	void EnsurePlayerPreviewWidgets();
	void SpawnPlayerPreview();
	void DestroyPlayerPreview();
	void RefreshPlayerPreview();
};
