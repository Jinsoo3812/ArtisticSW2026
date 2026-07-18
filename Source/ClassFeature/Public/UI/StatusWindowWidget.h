#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StatusWindowWidget.generated.h"

class ABasePlayer;
class UInventoryPanelWidget;
class UProgressBar;
class UQuickSlotEntryWidget;
class UTextBlock;

UCLASS()
class CLASSFEATURE_API UStatusWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
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
	TObjectPtr<UTextBlock> ExperienceText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ExperienceProgressBar;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;

	void RefreshQuickSlots();
	void HandleInventoryChanged();
};
