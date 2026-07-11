#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSpawnTypes.h"
#include "GlobalLootSpawnManager.generated.h"

class ALootZoneSpawnManager;

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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bInitializeOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bAutoDiscoverZoneManagers = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Budget", meta = (ClampMin = "0", UIMin = "0"))
	int32 TotalActivePointBudget = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Seed")
	int32 SpawnSeed = 20260708;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Budget")
	TArray<FLootZoneBudgetEntry> ZoneBudgets;

private:
	TMap<ALootZoneSpawnManager*, int32> CalculateZoneBudgets() const;
};
