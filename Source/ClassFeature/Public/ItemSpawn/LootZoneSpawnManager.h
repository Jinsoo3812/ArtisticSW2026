#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSpawnTypes.h"
#include "LootZoneSpawnManager.generated.h"

class ABaseItem;
class ALootSpawnPointBase;
class ALooseLootSpawnPoint;
class UDataTable;

UCLASS()
class CLASSFEATURE_API ALootZoneSpawnManager : public AActor
{
	GENERATED_BODY()

public:
	ALootZoneSpawnManager();

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	bool BuildSpawnPointList();

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	int32 ActivateAndSpawnByBudget(int32 Budget, int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	void ResetZone(bool bDestroySpawnedActors);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bAutoBuildSpawnPointListOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bAutoDiscoverOwnedSpawnPoints = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Data")
	TObjectPtr<UDataTable> ZoneLootItemTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	TSubclassOf<ABaseItem> DefaultLooseLootClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	TArray<TObjectPtr<ALooseLootSpawnPoint>> LooseLootSpawnPoints;

private:
	TArray<FZoneLootItemRow> GetZoneLootRows() const;
	bool PickWeightedPoint(const TArray<ALootSpawnPointBase*>& Candidates, FRandomStream& RandomStream, ALootSpawnPointBase*& OutPoint) const;
	bool PickWeightedLootRow(const TArray<FZoneLootItemRow>& Rows, FRandomStream& RandomStream, FZoneLootItemRow& OutRow) const;
};
