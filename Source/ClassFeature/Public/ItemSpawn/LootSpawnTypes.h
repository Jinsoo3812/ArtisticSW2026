#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "LootSpawnTypes.generated.h"

class ABaseItem;
class ALootZoneSpawnManager;

UENUM(BlueprintType)
enum class ELootBudgetAllocationMode : uint8
{
	Fixed,
	Weighted
};

USTRUCT(BlueprintType)
struct FZoneLootItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TSubclassOf<ABaseItem> ItemClassOverride = nullptr;
};

USTRUCT(BlueprintType)
struct FChestInitialLootRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1", UIMin = "1"))
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Weight = 1.f;
};

USTRUCT(BlueprintType)
struct FLootZoneBudgetEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<ALootZoneSpawnManager> ZoneManager = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "AllocationMode == ELootBudgetAllocationMode::Fixed", ClampMin = "0", UIMin = "0"))
	int32 FixedBudget = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "AllocationMode == ELootBudgetAllocationMode::Weighted", ClampMin = "0.0", UIMin = "0.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	ELootBudgetAllocationMode AllocationMode = ELootBudgetAllocationMode::Weighted;
};
