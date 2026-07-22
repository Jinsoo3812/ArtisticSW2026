#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "CraftingOutputReceiver.generated.h"

UINTERFACE(BlueprintType)
class CLASSFEATURE_API UCraftingOutputReceiver : public UInterface
{
	GENERATED_BODY()
};

/** Optional destination implemented by storage, quest, ship, or other systems. */
class CLASSFEATURE_API ICraftingOutputReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crafting")
	bool CanReceiveCraftedItem(FGameplayTag ItemTag, int32 Quantity, AActor* CraftingOwner) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crafting")
	bool ReceiveCraftedItem(FGameplayTag ItemTag, int32 Quantity, AActor* CraftingOwner);
};

