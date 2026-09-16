#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSpawnTypes.h"
#include "Balance/ProgressionBalanceData.h"
#include "GlobalLootSpawnManager.generated.h"

class ALootZoneSpawnManager;
class AChestSpawnPoint;
class URandomChestGroup;
class UShipUpgradeTreeDataAsset;
class UItemData;
class UDataTable;

UCLASS()
class CLASSFEATURE_API AGlobalLootSpawnManager : public AActor
{
	GENERATED_BODY()

public:
	AGlobalLootSpawnManager();

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	bool BuildZoneManagerList();

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	int32 InitializeLevelLoot();

	UFUNCTION(BlueprintCallable, Category = "Chest|Spawn")
	int32 InitializeDataDrivenChests();
	int32 InitializeDataDrivenChestsWithBalance(const UProgressionBalanceData* Balance);

	/** Call again after dynamically spawning a level's ships/chest points. Sunk chests are never counted. */
	UFUNCTION(BlueprintCallable, Category = "Chest|Progression")
	bool RebalanceSpawnedChests();
	bool RebalanceSpawnedChestsWithData(const UProgressionBalanceData* Balance, const UItemData* Items,
		const UDataTable* Recipes, const UShipUpgradeTreeDataAsset* Tree);

	UFUNCTION(BlueprintPure, Category = "Chest|Progression")
	int32 GetLastActiveChestCount(EProgressionZone Zone) const;

	/** Sunk chests are not counted in the zone budget; their chances are scaled deck-chest chances. */
	bool GetSunkChestDrops(EProgressionZone Zone, TArray<FProgressionComputedDrop>& OutDrops) const;

	/** Pure calculation used by the manager and automation tests. */
	static bool CalculateZoneDrops(EProgressionZone Zone, const FProgressionZoneTarget& Target,
		int32 ActiveChestCount, const UItemData* Items, const UDataTable* Recipes,
		const UShipUpgradeTreeDataAsset* Tree, TArray<FProgressionComputedDrop>& OutDrops);

	void SetInitializeOnBeginPlayForTesting(bool bInInitialize) { bInitializeOnBeginPlay = bInInitialize; }
	void SetSpawnSeedForTesting(int32 InSeed) 
	{ 
		SpawnSeed = InSeed; 
		bUseRandomSeed = false; 
	}

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bInitializeOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bAutoDiscoverZoneManagers = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Budget", meta = (ClampMin = "0", UIMin = "0"))
	int32 TotalActivePointBudget = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Seed")
	bool bUseRandomSeed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Seed", meta = (EditCondition = "!bUseRandomSeed"))
	int32 SpawnSeed = 20260708;

	/** Supplies the actual per-node activation costs; no ship costs are authored in Progression. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Progression")
	TSoftObjectPtr<UShipUpgradeTreeDataAsset> ShipUpgradeTree;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Budget")
	TArray<FLootZoneBudgetEntry> ZoneBudgets;

private:
	bool bProgressionFinalized = false;
	int32 LastActiveChestCounts[4] = {0, 0, 0, 0};
	TArray<FProgressionComputedDrop> LastZoneDrops[4];
	TMap<ALootZoneSpawnManager*, int32> CalculateZoneBudgets() const;
	static AChestSpawnPoint* PickWeightedChestPoint(
		const TArray<AChestSpawnPoint*>& Candidates,
		FRandomStream& RandomStream);
};
