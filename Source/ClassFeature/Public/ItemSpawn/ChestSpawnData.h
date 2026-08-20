#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Storage/StorageComponent.h"
#include "StoryFacadeSubsystem.h"
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

UENUM(BlueprintType)
enum class EChestEnvironment : uint8
{
	Land UMETA(DisplayName = "지상 (Land)"),
	Water UMETA(DisplayName = "해상/바다 (Water)")
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

	/** 스토리 조건부 확정 드랍 퀘스트 아이템 (스토리 조건 충족 시 추가 슬롯에 100% 드랍) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Quest")
	FGameplayTag GuaranteedQuestItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Quest", meta = (ClampMin = "1", UIMin = "1"))
	int32 GuaranteedQuestItemCount = 1;

	/** 이 스토리 노드에 도달했을 때부터 퀘스트 아이템이 포함됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Quest")
	EStoryNode RequiredStoryNodeForQuestItem = EStoryNode::GameStarted;

	/** 처치 노드 도달 후 더 이상 퀘스트 아이템을 드랍하지 않을지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Quest")
	bool bStopAfterStoryNode = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Quest",
		meta = (EditCondition = "bStopAfterStoryNode"))
	EStoryNode StopAfterStoryNodeForQuestItem = EStoryNode::MiddleBoss1Defeated;

	TArray<FStorageItemEntry> RollInitialItems(int32 Seed, const UObject* WorldContextObject = nullptr) const;

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
