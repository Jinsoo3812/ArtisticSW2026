#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Storage/StorageComponent.h"
#include "ChestSpawnData.generated.h"

class AStorageChest;
class UDataTable;

UENUM(BlueprintType)
enum class EChestSpawnMode : uint8
{
	Legacy,
	Random,
	Guarded
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> LootTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0", UIMin = "0"))
	int32 RollCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 SlotCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 ColumnCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics")
	bool bEnablePhysicsAndBuoyancy = false;

	TArray<FStorageItemEntry> RollInitialItems(int32 Seed) const;

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UChestDefinition> ChestDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0", UIMin = "0"))
	int32 SpawnCount = 1;
};
