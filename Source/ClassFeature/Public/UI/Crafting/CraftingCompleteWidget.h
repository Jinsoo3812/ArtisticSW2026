#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/CraftingTypes.h"
#include "CraftingCompleteWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;

DECLARE_DELEGATE(FOnCraftingCompleteDismissed);

/** Success-only page embedded in WBP_CraftingPanel's WidgetSwitcher. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UCraftingCompleteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowCraftedItem(const FCraftingListEntry& Item);

	FOnCraftingCompleteDismissed OnDismissed;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CraftedItemIconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CraftedItemNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ContinueButton;

private:
	UFUNCTION()
	void HandleContinueClicked();
};
