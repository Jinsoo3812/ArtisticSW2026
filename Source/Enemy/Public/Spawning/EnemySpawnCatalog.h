#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "EnemySpawnCatalog.generated.h"

class ABaseEnemy;

/** Exact tag-to-class and balance-row mapping used by encounter spawners. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemySpawnCatalogEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (Categories = "Enemy.Type"))
	FGameplayTag EnemyTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	TSubclassOf<ABaseEnemy> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (RowType = "/Script/Enemy.EnemyBaseStatsRow"))
	FDataTableRowHandle StatsRow;
};

UCLASS(BlueprintType)
class ENEMY_API UEnemySpawnCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Enemy Spawn")
	bool FindDefinition(FGameplayTag EnemyTypeTag, FEnemySpawnCatalogEntry& OutDefinition) const;

	const TArray<FEnemySpawnCatalogEntry>& GetEntries() const { return Entries; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (TitleProperty = "EnemyTypeTag"))
	TArray<FEnemySpawnCatalogEntry> Entries;
};
