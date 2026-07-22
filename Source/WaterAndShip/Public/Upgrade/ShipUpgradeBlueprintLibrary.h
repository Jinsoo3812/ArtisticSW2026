#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "ShipUpgradeBlueprintLibrary.generated.h"

class APlayerState;
class UShipUpgradeComponent;

UCLASS()
class WATERANDSHIP_API UShipUpgradeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Ship Upgrade", meta = (WorldContext = "WorldContextObject"))
	static UShipUpgradeComponent* GetLocalShipUpgradeComponent(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade")
	static UShipUpgradeComponent* GetShipUpgradeComponent(APlayerState* PlayerState);

	/** Convenience constructor for editor utilities and programmatic tree generation. */
	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Data")
	static FCraftingItemStack MakeShipUpgradeMaterialCost(FName ItemTagName, int32 Quantity);
};
