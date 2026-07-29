#include "ItemSpawn/LootZoneSpawnManager.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Item/BaseItem.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Storage/StorageChest.h"

ALootZoneSpawnManager::ALootZoneSpawnManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void ALootZoneSpawnManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bAutoBuildSpawnPointListOnBeginPlay)
	{
		BuildSpawnPointList();
	}
}

bool ALootZoneSpawnManager::BuildSpawnPointList()
{
	LooseLootSpawnPoints.RemoveAll([](const TObjectPtr<ALooseLootSpawnPoint>& Point)
	{
		return !IsValid(Point);
	});
	ChestSpawnPoints.RemoveAll([](const TObjectPtr<AChestSpawnPoint>& Point)
	{
		return !IsValid(Point);
	});

	if (!bAutoDiscoverOwnedSpawnPoints)
	{
		return LooseLootSpawnPoints.Num() > 0 || ChestSpawnPoints.Num() > 0;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<ALooseLootSpawnPoint> It(World); It; ++It)
	{
		ALooseLootSpawnPoint* Point = *It;
		const bool bZoneIdMatches = !ZoneId.IsNone() && Point->GetZoneId() == ZoneId;
		if (IsValid(Point) && (Point->GetOwner() == this || bZoneIdMatches))
		{
			LooseLootSpawnPoints.AddUnique(Point);
		}
	}

	for (TActorIterator<AChestSpawnPoint> It(World); It; ++It)
	{
		AChestSpawnPoint* Point = *It;
		const bool bZoneIdMatches = !ZoneId.IsNone() && Point->GetZoneId() == ZoneId;
		if (IsValid(Point) && (Point->GetOwner() == this || bZoneIdMatches))
		{
			ChestSpawnPoints.AddUnique(Point);
		}
	}

	return LooseLootSpawnPoints.Num() > 0 || ChestSpawnPoints.Num() > 0;
}

int32 ALootZoneSpawnManager::ActivateAndSpawnByBudget(int32 Budget, int32 Seed)
{
	if (!HasAuthority() || Budget <= 0)
	{
		return 0;
	}

	if (bAutoBuildSpawnPointListOnBeginPlay && LooseLootSpawnPoints.Num() == 0 && ChestSpawnPoints.Num() == 0)
	{
		BuildSpawnPointList();
	}

	const TArray<FZoneLootItemRow> ZoneLootRows = GetZoneLootRows();
	const TArray<FChestInitialLootRow> ChestLootRows = GetChestLootRows();

	TArray<ALootSpawnPointBase*> Candidates;
	for (ALooseLootSpawnPoint* Point : LooseLootSpawnPoints)
	{
		if (IsValid(Point) && Point->CanBeActivated() && ZoneLootRows.Num() > 0)
		{
			Candidates.Add(Point);
		}
	}
	for (AChestSpawnPoint* Point : ChestSpawnPoints)
	{
		if (IsValid(Point) && !Point->IsDataDrivenChestPoint() && Point->CanBeActivated())
		{
			Candidates.Add(Point);
		}
	}

	FRandomStream RandomStream(Seed);
	int32 ActivatedCount = 0;

	while (ActivatedCount < Budget && Candidates.Num() > 0)
	{
		ALootSpawnPointBase* SelectedPoint = nullptr;
		if (!PickWeightedPoint(Candidates, RandomStream, SelectedPoint) || !SelectedPoint)
		{
			break;
		}

		bool bSpawned = false;
		if (ALooseLootSpawnPoint* LoosePoint = Cast<ALooseLootSpawnPoint>(SelectedPoint))
		{
			FZoneLootItemRow LootRow;
			if (PickWeightedLootRow(ZoneLootRows, RandomStream, LootRow))
			{
				bSpawned = IsValid(LoosePoint->SpawnLooseLoot(LootRow, DefaultLooseLootClass));
			}
		}
		else if (AChestSpawnPoint* ChestPoint = Cast<AChestSpawnPoint>(SelectedPoint))
		{
			bSpawned = IsValid(ChestPoint->SpawnChest(ChestLootRows, DefaultChestClass, RandomStream.RandRange(1, MAX_int32)));
		}

		Candidates.Remove(SelectedPoint);
		if (bSpawned)
		{
			++ActivatedCount;
		}
	}

	return ActivatedCount;
}

void ALootZoneSpawnManager::ResetZone(bool bDestroySpawnedActors)
{
	for (ALooseLootSpawnPoint* Point : LooseLootSpawnPoints)
	{
		if (IsValid(Point))
		{
			Point->ResetSpawnPoint(bDestroySpawnedActors);
		}
	}

	for (AChestSpawnPoint* Point : ChestSpawnPoints)
	{
		if (IsValid(Point))
		{
			Point->ResetSpawnPoint(bDestroySpawnedActors);
		}
	}
}

TArray<FZoneLootItemRow> ALootZoneSpawnManager::GetZoneLootRows() const
{
	TArray<FZoneLootItemRow> Rows;
	if (ZoneLootItemTable)
	{
		TArray<FZoneLootItemRow*> RowPtrs;
		ZoneLootItemTable->GetAllRows(TEXT("ZoneLootItemTable"), RowPtrs);
		for (const FZoneLootItemRow* RowPtr : RowPtrs)
		{
			if (RowPtr)
			{
				Rows.Add(*RowPtr);
			}
		}
	}
	return Rows;
}

TArray<FChestInitialLootRow> ALootZoneSpawnManager::GetChestLootRows() const
{
	TArray<FChestInitialLootRow> Rows;
	if (ChestInitialLootTable)
	{
		TArray<FChestInitialLootRow*> RowPtrs;
		ChestInitialLootTable->GetAllRows(TEXT("ChestInitialLootTable"), RowPtrs);
		for (const FChestInitialLootRow* RowPtr : RowPtrs)
		{
			if (RowPtr)
			{
				Rows.Add(*RowPtr);
			}
		}
	}
	return Rows;
}

bool ALootZoneSpawnManager::PickWeightedPoint(const TArray<ALootSpawnPointBase*>& Candidates, FRandomStream& RandomStream, ALootSpawnPointBase*& OutPoint) const
{
	OutPoint = nullptr;

	float TotalWeight = 0.f;
	for (const ALootSpawnPointBase* Point : Candidates)
	{
		if (IsValid(Point))
		{
			TotalWeight += FMath::Max(0.f, Point->GetPointWeight());
		}
	}

	if (TotalWeight <= 0.f)
	{
		return false;
	}

	const float Pick = RandomStream.FRandRange(0.f, TotalWeight);
	float AccumulatedWeight = 0.f;
	for (ALootSpawnPointBase* Point : Candidates)
	{
		if (!IsValid(Point))
		{
			continue;
		}

		AccumulatedWeight += FMath::Max(0.f, Point->GetPointWeight());
		if (Pick <= AccumulatedWeight)
		{
			OutPoint = Point;
			return true;
		}
	}

	return false;
}

bool ALootZoneSpawnManager::PickWeightedLootRow(const TArray<FZoneLootItemRow>& Rows, FRandomStream& RandomStream, FZoneLootItemRow& OutRow) const
{
	float TotalWeight = 0.f;
	for (const FZoneLootItemRow& Row : Rows)
	{
		if (Row.ItemTag.IsValid() && Row.Weight > 0.f)
		{
			TotalWeight += Row.Weight;
		}
	}

	if (TotalWeight <= 0.f)
	{
		return false;
	}

	const float Pick = RandomStream.FRandRange(0.f, TotalWeight);
	float AccumulatedWeight = 0.f;
	for (const FZoneLootItemRow& Row : Rows)
	{
		if (!Row.ItemTag.IsValid() || Row.Weight <= 0.f)
		{
			continue;
		}

		AccumulatedWeight += Row.Weight;
		if (Pick <= AccumulatedWeight)
		{
			OutRow = Row;
			return true;
		}
	}

	return false;
}
