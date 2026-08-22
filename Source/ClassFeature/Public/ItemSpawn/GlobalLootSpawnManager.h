#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSpawnTypes.h"
#include "GlobalLootSpawnManager.generated.h"

class ALootZoneSpawnManager;
class AChestSpawnPoint;
class URandomChestGroup;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Budget")
	TArray<FLootZoneBudgetEntry> ZoneBudgets;

private:
	TMap<ALootZoneSpawnManager*, int32> CalculateZoneBudgets() const;
	static AChestSpawnPoint* PickWeightedChestPoint(
		const TArray<AChestSpawnPoint*>& Candidates,
		FRandomStream& RandomStream);
};
