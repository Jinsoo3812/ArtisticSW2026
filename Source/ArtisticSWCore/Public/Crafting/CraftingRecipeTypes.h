#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "CraftingRecipeTypes.generated.h"

namespace ArtisticCrafting
{
	/** WBP_CraftingPanel exposes North, East, South, and West material slots. */
	inline constexpr int32 MaxIngredientSlots = 4;
}

UENUM(BlueprintType)
enum class EProgressionRecipeTrack : uint8
{
	None,
	Weapon,
	Consumable
};

/** One item stack used by a crafting recipe or inventory transaction. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FCraftingItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (Categories = "Item.Id"))
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (ClampMin = "1"))
	int32 Quantity = 1;
};

/** Data-driven recipe used by the new crafting pipeline. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FCraftingRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (Categories = "Item.Id"))
	FGameplayTag ResultItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (ClampMin = "1"))
	int32 ResultQuantity = 1;

	/** Empty means the recipe is available without owning a recipe item.
	 * Tier-four weapons each require their own reusable recipe item. Acquisition
	 * is configured independently from automated progression material drops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (Categories = "Item.Id"))
	FGameplayTag RequiredRecipeItemTag;

	/** Recipe items are knowledge tokens by default. Enable only for one-use recipes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting")
	bool bConsumeRecipeItem = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting")
	TArray<FCraftingItemStack> Ingredients;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting")
	int32 SortOrder = 0;

	/** 0 excludes this recipe from automatic chest demand; 1..4 are upgrades from base tier 0. */
	UPROPERTY()
	int32 ProgressionTier = 0;

	UPROPERTY()
	EProgressionRecipeTrack ProgressionTrack = EProgressionRecipeTrack::None;
};

