#include "ItemSpawn/ChestSpawnData.h"

#include "Engine/DataTable.h"
#include "ItemSpawn/LootSpawnTypes.h"

int32 UChestDefinition::GetEffectiveRollCount() const
{
	if (BalanceProfile)
	{
		TArray<FProgressionComputedDrop> Drops;
		BalanceProfile->GetComputedDrops(BalanceZone, BalanceKind, Drops);
		return Drops.Num();
	}
	return RollCount;
}

int32 UChestDefinition::GetEffectiveSlotCount() const
{
	return BalanceProfile ? FMath::Max(SlotCount, GetEffectiveRollCount()) : SlotCount;
}

int32 UChestDefinition::GetEffectiveColumnCount() const
{
	return ColumnCount;
}

TArray<FStorageItemEntry> UChestDefinition::RollInitialItems(int32 Seed, float ExpectedValueRatio) const
{
	if (BalanceProfile)
	{
		TArray<FStorageItemEntry> Items;
		TArray<FProgressionComputedDrop> Drops;
		BalanceProfile->GetComputedDrops(BalanceZone, BalanceKind, Drops);
		FRandomStream Stream(Seed);
		for (const FProgressionComputedDrop& Drop : Drops)
		{
			if (Stream.FRand() >= Drop.Chance * FMath::Clamp(ExpectedValueRatio, 0.f, 1.f)) continue;
			FStorageItemEntry& Item = Items.AddDefaulted_GetRef();
			Item.ItemTag = Drop.ItemTag;
			Item.Count = Stream.RandRange(Drop.MinCount, Drop.MaxCount);
		}
		return Items;
	}
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

	const float DesiredRolls = FMath::Max(0.f, GetEffectiveRollCount() * FMath::Clamp(ExpectedValueRatio, 0.f, 1.f));
	int32 ActualRolls = FMath::FloorToInt(DesiredRolls);
	FRandomStream RatioStream(Seed ^ 0x71B4A27);
	if (RatioStream.FRand() < DesiredRolls - ActualRolls)
	{
		++ActualRolls;
	}
	return RollItemsFromRows(Rows, ActualRolls, Seed);
}

int32 URandomChestGroup::GetEffectiveSpawnCount() const
{
	return SpawnCount;
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
