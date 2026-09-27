#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Storage/StorageComponent.h"
#include "StoryFacadeSubsystem.h"
#include "Balance/ProgressionBalanceData.h"
#include "ChestSpawnData.generated.h"

class AStorageChest;
class UDataTable;

UENUM(BlueprintType)
enum class EChestSpawnMode : uint8
{
	Random,
	Guarded
};

UENUM(BlueprintType)
enum class EChestEnvironment : uint8
{
	Land UMETA(DisplayName = "지상 (Land)"),
	Water UMETA(DisplayName = "바다 위 (Ocean)"),
	ShipDeck UMETA(DisplayName = "배 위 (Ship Deck)")
};

/**
 * Reusable definition of what chest to spawn and how to fill it.
 * Level-specific placement and guard references stay on AChestSpawnPoint.
 */
UCLASS(BlueprintType)
class CLASSFEATURE_API UChestDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest")
	TSubclassOf<AStorageChest> ChestClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Snapshot", meta = (EditCondition = "BalanceProfile == nullptr"))
	TObjectPtr<UDataTable> LootTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Fallback", meta = (ClampMin = "0", UIMin = "0", EditCondition = "BalanceProfile == nullptr"))
	int32 RollCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Fallback", meta = (ClampMin = "1", UIMin = "1", EditCondition = "BalanceProfile == nullptr"))
	int32 SlotCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Fallback", meta = (ClampMin = "1", UIMin = "1", EditCondition = "BalanceProfile == nullptr"))
	int32 ColumnCount = 4;

	/** When assigned, live balance values take precedence over the authored DT snapshot above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TObjectPtr<UProgressionBalanceData> BalanceProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	EProgressionZone BalanceZone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	EProgressionChestKind BalanceKind = EProgressionChestKind::ShipGuarded;

	int32 GetEffectiveRollCount() const;
	int32 GetEffectiveSlotCount() const;
	int32 GetEffectiveColumnCount() const;
	TArray<FStorageItemEntry> RollInitialItems(int32 Seed, float ExpectedValueRatio = 1.f) const;

	static TArray<FStorageItemEntry> RollItemsFromRows(
		const TArray<struct FChestInitialLootRow>& LootRows,
		int32 RollCount,
		int32 Seed);
};

/**
 * Random spawn group shared by multiple level AChestSpawnPoint instances.
 * SpawnCount points are selected without replacement using each point's weight.
 */
UCLASS(BlueprintType)
class CLASSFEATURE_API URandomChestGroup : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Legacy serialized reference. Progression random groups only select active points. */
	UPROPERTY()
	TObjectPtr<UChestDefinition> ChestDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn|Fallback", meta = (ClampMin = "0", UIMin = "0", EditCondition = "BalanceProfile == nullptr"))
	int32 SpawnCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TObjectPtr<UProgressionBalanceData> BalanceProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	EProgressionZone BalanceZone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	EProgressionChestKind BalanceKind = EProgressionChestKind::OceanRandom;

	int32 GetEffectiveSpawnCount() const;
};
