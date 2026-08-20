#include "ItemSpawn/ChestSpawnData.h"

#include "Engine/DataTable.h"
#include "ItemSpawn/LootSpawnTypes.h"

TArray<FStorageItemEntry> UChestDefinition::RollInitialItems(int32 Seed, const UObject* WorldContextObject) const
{
	TArray<FChestInitialLootRow> Rows;
	if (LootTable)
	{
		TArray<FChestInitialLootRow*> RowPointers;
		LootTable->GetAllRows(TEXT("ChestDefinition"), RowPointers);
		for (const FChestInitialLootRow* Row : RowPointers)
		{
			if (Row)
			{
				Rows.Add(*Row);
			}
		}
	}

	TArray<FStorageItemEntry> RolledItems = RollItemsFromRows(Rows, RollCount, Seed);

	// 스토리 조건부 퀘스트 확정 아이템 추가 검사
	if (GuaranteedQuestItemTag.IsValid())
	{
		bool bShouldIncludeQuestItem = true;
		if (WorldContextObject)
		{
			if (const UWorld* World = WorldContextObject->GetWorld())
			{
				if (const UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (const UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
					{
						const bool bRequiredReached = Story->IsStoryNodeReached(RequiredStoryNodeForQuestItem);
						const bool bStoppedReached = bStopAfterStoryNode && Story->IsStoryNodeReached(StopAfterStoryNodeForQuestItem);
						bShouldIncludeQuestItem = bRequiredReached && !bStoppedReached;
					}
				}
			}
		}

		if (bShouldIncludeQuestItem)
		{
			FStorageItemEntry& QuestItem = RolledItems.AddDefaulted_GetRef();
			QuestItem.ItemTag = GuaranteedQuestItemTag;
			QuestItem.Count = FMath::Max(1, GuaranteedQuestItemCount);
		}
	}

	return RolledItems;
}

TArray<FStorageItemEntry> UChestDefinition::RollItemsFromRows(
	const TArray<FChestInitialLootRow>& LootRows,
	int32 InRollCount,
	int32 Seed)
{
	TArray<FStorageItemEntry> Items;
	if (InRollCount <= 0 || LootRows.IsEmpty())
	{
		return Items;
	}

	float TotalWeight = 0.f;
	for (const FChestInitialLootRow& Row : LootRows)
	{
		if (Row.ItemTag.IsValid() && Row.Weight > 0.f)
		{
			TotalWeight += Row.Weight;
		}
	}

	if (TotalWeight <= 0.f)
	{
		return Items;
	}

	FRandomStream RandomStream(Seed);
	for (int32 RollIndex = 0; RollIndex < InRollCount; ++RollIndex)
	{
		const float Pick = RandomStream.FRandRange(0.f, TotalWeight);
		float AccumulatedWeight = 0.f;

		for (const FChestInitialLootRow& Row : LootRows)
		{
			if (!Row.ItemTag.IsValid() || Row.Weight <= 0.f)
			{
				continue;
			}

			AccumulatedWeight += Row.Weight;
			if (Pick > AccumulatedWeight)
			{
				continue;
			}

			FStorageItemEntry& Item = Items.AddDefaulted_GetRef();
			Item.ItemTag = Row.ItemTag;
			const int32 MinCount = FMath::Max(1, Row.MinCount);
			Item.Count = RandomStream.RandRange(MinCount, FMath::Max(MinCount, Row.MaxCount));
			break;
		}
	}

	return Items;
}
