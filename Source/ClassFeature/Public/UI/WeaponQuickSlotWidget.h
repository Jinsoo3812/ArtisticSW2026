#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponQuickSlotWidget.generated.h"

class ABasePlayer;
class UImage;

/**
 * HUD-only weapon quick slot widget.
 *
 * This widget owns every weapon slot shown on the HUD. Widget layout and image
 * brushes remain entirely designer-authored in WBP_WeaponQuickSlot.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API UWeaponQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForPlayer(ABasePlayer* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Weapon Quick Slot")
	void RefreshSlots();

protected:
	virtual void NativeDestruct() override;

	/** Image component: WeaponImage1 (BasePlayer QuickSlots index 0). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> WeaponImage1;

	/** Image component: SlotNumber1 (BasePlayer QuickSlots index 0). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlotNumber1;

	/** Image component: WeaponImage2 (BasePlayer QuickSlots index 1). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> WeaponImage2;

	/** Image component: SlotNumber2 (BasePlayer QuickSlots index 1). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlotNumber2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Quick Slot|Style")
	FLinearColor DefaultSlotNumberColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Quick Slot|Style")
	FLinearColor SelectedSlotNumberColor = FLinearColor(1.0f, 0.82f, 0.2f, 0.75f);

private:
	void UnbindPlayer();
	void RefreshSlot(int32 QuickSlotIndex, UImage* WeaponImage, UImage* SlotNumber) const;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;
};
