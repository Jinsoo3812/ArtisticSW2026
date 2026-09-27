#pragma once

#include "CoreMinimal.h"
#include "Balance/ProgressionBalanceData.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FixedChestDropData.generated.h"

/** Independent chance for one item in one progression zone, per spawned chest. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FFixedChestZoneChance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed Chest Drop")
	EProgressionZone Zone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed Chest Drop", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
	float ChancePercent = 0.f;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FFixedChestDropEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed Chest Drop", meta = (Categories = "Item.Id"))
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed Chest Drop", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	/** Omit a zone to disable this item there. Each zone may appear at most once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed Chest Drop")
	TArray<FFixedChestZoneChance> ZoneChances;

	float GetChance(EProgressionZone Zone) const;
};

/** Extra chest rolls, independent of the progression expected-value material budget. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UFixedChestDropData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Chest Drop")
	TArray<FFixedChestDropEntry> Drops;
};
