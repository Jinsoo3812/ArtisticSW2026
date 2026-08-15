#pragma once

#include "CoreMinimal.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "UObject/Interface.h"
#include "DialogueInventoryProvider.generated.h"

UINTERFACE(MinimalAPI)
class UDialogueInventoryProvider : public UInterface
{
	GENERATED_BODY()
};

/** Inventory seam used by NPCDialogue without depending on the player implementation module. */
class NPCDIALOGUE_API IDialogueInventoryProvider
{
	GENERATED_BODY()

public:
	virtual int32 GetDialogueItemCount(const FGameplayTag& ItemTag) const = 0;
	virtual bool CanApplyDialogueItemTransaction(
		const TArray<FCraftingItemStack>& RemovedItems,
		const TArray<FCraftingItemStack>& AddedItems) const = 0;
	virtual bool ApplyDialogueItemTransaction(
		const TArray<FCraftingItemStack>& RemovedItems,
		const TArray<FCraftingItemStack>& AddedItems) = 0;
};
