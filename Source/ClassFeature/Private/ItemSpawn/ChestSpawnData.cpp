#include "ItemSpawn/ChestSpawnData.h"

#include "Engine/DataTable.h"
#include "ItemSpawn/LootSpawnTypes.h"

TArray<FStorageItemEntry> UChestDefinition::RollInitialItems(int32 Seed) const
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

	return RollItemsFromRows(Rows, RollCount, Seed);
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
