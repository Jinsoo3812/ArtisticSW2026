#pragma once

#include "CoreMinimal.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ProgressionBalanceData.generated.h"

UENUM(BlueprintType)
enum class EProgressionZone : uint8
{
	Mid1,
	Mid2,
	Mid3,
	Final
};

UENUM(BlueprintType)
enum class EProgressionChestKind : uint8
{
	ShipGuarded,
	IslandGuarded,
	OceanRandom,
	IslandRandom
};

UENUM(BlueprintType)
enum class EProgressionMaterialTrack : uint8
{
	Weapon,
	Consumable,
	Ship
};

/** One weighted draw. Any later special material can be added without changing the roll code. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FName RowName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (Categories = "Item.Id"))
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MaxCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	float Weight = 1.f;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionChestPool
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EProgressionZone Zone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EProgressionChestKind Kind = EProgressionChestKind::ShipGuarded;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 RollCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage", meta = (ClampMin = "1"))
	int32 SlotCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage", meta = (ClampMin = "1"))
	int32 ColumnCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FProgressionLootEntry> Entries;
};

/** Counts are per zone and per level regeneration, not shared across the whole map. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionZonePlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	EProgressionZone Zone = EProgressionZone::Mid1;

	/** Legacy audit input; guarded chests are counted from spawned points at runtime. */
	UPROPERTY()
	int32 ShipSquads = 3;

	UPROPERTY()
	int32 ShipsPerSquad = 3;

	UPROPERTY()
	int32 IslandGuardSquads = 3;

	/** Legacy estimate; candidate points are discovered from the level at runtime. */
	UPROPERTY()
	int32 IslandCandidatePoints = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Random", meta = (ClampMin = "0"))
	int32 IslandActiveChests = 5;

	/** Legacy estimate; candidate points are discovered from the level at runtime. */
	UPROPERTY()
	int32 OceanCandidatePoints = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Random", meta = (ClampMin = "0"))
	int32 OceanActiveChests = 6;

	/** Legacy demand inputs; runtime uses ZoneTargets instead. */
	UPROPERTY()
	int32 WeaponCraftCount = 1;

	UPROPERTY()
	int32 ConsumableCraftCount = 3;

	UPROPERTY()
	int32 ShipUpgradeNodeCount = 3;
};

/** An independently rolled material stack calculated from recipes and the zone plan. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionComputedDrop
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FGameplayTag ItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	float Chance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 MinCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 MaxCount = 1;
};

/** Only the four progression targets are authored; chest counts are discovered at runtime. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionZoneTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	EProgressionZone Zone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "1"))
	int32 FullClears = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "0"))
	int32 WeaponCrafts = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "0"))
	int32 ConsumableCrafts = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "0"))
	int32 ShipUpgrades = 0;
};

/** The basic N/N-1 pair; AdditionalIngredients is an open-ended special-material hook. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionMaterialCost
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EProgressionMaterialTrack Track = EProgressionMaterialTrack::Weapon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Tier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Basic", meta = (Categories = "Item.Id"))
	FGameplayTag BaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Basic", meta = (ClampMin = "0"))
	int32 BaseQuantity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Basic", meta = (Categories = "Item.Id"))
	FGameplayTag PremiumMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Basic", meta = (ClampMin = "0"))
	int32 PremiumQuantity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	TArray<FCraftingItemStack> AdditionalIngredients;
};

/** Optional explicit recipe mapping, including new items with no naming convention. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProgressionRecipeBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName RecipeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	EProgressionMaterialTrack Track = EProgressionMaterialTrack::Weapon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Tier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	TArray<FCraftingItemStack> AdditionalIngredients;
};

UCLASS(BlueprintType)
class ARTISTICSWCORE_API UProgressionBalanceData : public UDataAsset
{
	GENERATED_BODY()

public:
	UProgressionBalanceData();

	/** Changing this changes the progression target used by the balance audit. */
	UPROPERTY()
	int32 TargetFullClearsPerZone = 2;

	/** A sunken ship always spawns a chest but receives this fraction of deck-chest rolls in expectation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sunk Chest", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SunkChestExpectedValueRatio = 0.5f;

	UPROPERTY()
	int32 RepresentativeWeaponCraftCount = 1;

	UPROPERTY()
	int32 RepresentativeConsumableCraftCount = 3;

	UPROPERTY()
	int32 ShipUpgradeNodesPerTier = 3;

	/** Per-zone random chest activation targets; placed candidates are counted at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	TArray<FProgressionZonePlan> ZonePlans;

	/** New runtime-count balance input. Fill exactly one row for each of the four zones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	TArray<FProgressionZoneTarget> ZoneTargets;

	UPROPERTY()
	TArray<FProgressionChestPool> ChestPools;

	/** Optional special drops in every active chest. Weight is interpreted as an independent 0..1 probability here. */
	UPROPERTY()
	TArray<FProgressionLootEntry> GlobalLootEntries;

	/** Ship node costs are authored here because ship upgrades are not item-crafting recipes. */
	UPROPERTY()
	TArray<FProgressionMaterialCost> ShipUpgradeCosts;

	UPROPERTY()
	TArray<FProgressionMaterialCost> MaterialCosts;

	UPROPERTY()
	TArray<FProgressionRecipeBinding> RecipeBindings;

	const FProgressionZonePlan* FindZone(EProgressionZone Zone) const;
	const FProgressionZoneTarget* FindTarget(EProgressionZone Zone) const;
	const FProgressionChestPool* FindPool(EProgressionZone Zone, EProgressionChestKind Kind) const;
	const FProgressionMaterialCost* FindCost(EProgressionMaterialTrack Track, int32 Tier) const;
	bool GetIngredients(EProgressionMaterialTrack Track, int32 Tier, TArray<FCraftingItemStack>& OutIngredients) const;
	bool GetRecipeIngredients(FName RecipeId, const FGameplayTag& ResultTag, TArray<FCraftingItemStack>& OutIngredients) const;
	int32 GetActiveCount(EProgressionZone Zone, EProgressionChestKind Kind) const;
	bool GetTierDemand(EProgressionZone Zone, TMap<FGameplayTag, int32>& OutDemand) const;
	UFUNCTION(BlueprintCallable, Category = "Balance|Loot")
	void GetComputedDrops(EProgressionZone Zone, EProgressionChestKind Kind,
		TArray<FProgressionComputedDrop>& OutDrops) const;
	UFUNCTION(BlueprintCallable, Category = "Balance|Audit")
	float GetExpectedQuantity(EProgressionZone Zone, FGameplayTag MaterialTag,
		int32 FullClears = 2, bool bSinkShipsInsteadOfBoarding = false) const;
	UFUNCTION(BlueprintCallable, Category = "Balance|Audit")
	int32 GetRepresentativeNextTierRequirement(EProgressionZone Zone, FGameplayTag MaterialTag) const;
	bool ValidateBalance(TArray<FString>& OutErrors) const;

	static const UProgressionBalanceData* LoadConfigured();
};
