#pragma once

#include "CoreMinimal.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "UObject/Interface.h"
#include "ShipUpgradeInventoryProvider.generated.h"

DECLARE_MULTICAST_DELEGATE(FShipUpgradeInventoryChangedNative);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UShipUpgradeInventoryProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Narrow inventory contract consumed by the ship-upgrade module.
 * ClassFeature implements it without creating a WaterAndShip -> ClassFeature dependency cycle.
 */
class WATERANDSHIP_API IShipUpgradeInventoryProvider
{
	GENERATED_BODY()

public:
	virtual int32 GetShipUpgradeItemCount(const FGameplayTag& ItemTag) const = 0;
	virtual bool RemoveShipUpgradeItemsAtomically(const TArray<FCraftingItemStack>& Costs) = 0;
	virtual bool AddShipUpgradeItemsAtomically(const TArray<FCraftingItemStack>& Items) = 0;
	virtual FShipUpgradeInventoryChangedNative& GetShipUpgradeInventoryChangedDelegate() = 0;
};
