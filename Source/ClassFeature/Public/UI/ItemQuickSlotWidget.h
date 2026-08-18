#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemQuickSlotWidget.generated.h"

class ABasePlayer;
class UBorder;
class UImage;
class UOverlay;
class UTextBlock;

/** HUD container that owns consumable quick slots 3, 4, and 5. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UItemQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForPlayer(ABasePlayer* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Item Quick Slot")
	void RefreshSlots();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> PressedHighlightBorder3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> ItemInfoOverlay3;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage4;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText4;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText4;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> PressedHighlightBorder4;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> ItemInfoOverlay4;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage5;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText5;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText5;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> PressedHighlightBorder5;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> ItemInfoOverlay5;

private:
	void UnbindPlayer();
	void RefreshSlot(int32 QuickSlotIndex, UImage* IconImage, UTextBlock* NameText,
		UTextBlock* CountText, UBorder* PressedHighlightBorder,
		UOverlay* ItemInfoOverlay) const;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;
};
