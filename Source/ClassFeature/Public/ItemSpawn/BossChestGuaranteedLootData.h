#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Storage/StorageComponent.h"
#include "BossChestGuaranteedLootData.generated.h"

class ABaseCharacter;

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FGuaranteedBossLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Boss")
	TSoftClassPtr<ABaseCharacter> BossClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Boss")
	TArray<FStorageItemEntry> GuaranteedItems;
};

UCLASS(BlueprintType)
class CLASSFEATURE_API UBossChestGuaranteedLootData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Boss")
	TArray<FGuaranteedBossLootEntry> Entries;

	bool FindItemsForExactClass(const UClass* BossClass, TArray<FStorageItemEntry>& OutItems) const;
};
