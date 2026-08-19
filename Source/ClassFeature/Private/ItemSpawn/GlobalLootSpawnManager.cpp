#include "ItemSpawn/GlobalLootSpawnManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ItemSpawn/LootZoneSpawnManager.h"
#include "Storage/StorageChest.h"

AGlobalLootSpawnManager::AGlobalLootSpawnManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void AGlobalLootSpawnManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bInitializeOnBeginPlay)
	{
		InitializeLevelLoot();
	}
}

bool AGlobalLootSpawnManager::BuildZoneManagerList()
{
	ZoneBudgets.RemoveAll([](const FLootZoneBudgetEntry& Entry)
	{
		return !IsValid(Entry.ZoneManager);
	});

	if (!bAutoDiscoverZoneManagers)
	{
		return ZoneBudgets.Num() > 0;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<ALootZoneSpawnManager> It(World); It; ++It)
	{
		ALootZoneSpawnManager* ZoneManager = *It;
		if (!IsValid(ZoneManager))
		{
			continue;
		}

		const bool bAlreadyRegistered = ZoneBudgets.ContainsByPredicate([ZoneManager](const FLootZoneBudgetEntry& Entry)
		{
			return Entry.ZoneManager == ZoneManager;
		});

		if (!bAlreadyRegistered)
		{
			FLootZoneBudgetEntry Entry;
			Entry.ZoneManager = ZoneManager;
			Entry.AllocationMode = ELootBudgetAllocationMode::Weighted;
			ZoneBudgets.Add(Entry);
		}
	}

	return ZoneBudgets.Num() > 0;
}

int32 AGlobalLootSpawnManager::InitializeLevelLoot()
{
	if (!HasAuthority())
	{
		return 0;
	}

	int32 ActivatedCount = InitializeDataDrivenChests();

	if (ZoneBudgets.Num() == 0 || bAutoDiscoverZoneManagers)
	{
		BuildZoneManagerList();
	}

	const TMap<ALootZoneSpawnManager*, int32> BudgetsByZone = CalculateZoneBudgets();
	int32 ZoneIndex = 0;

	for (const TPair<ALootZoneSpawnManager*, int32>& Pair : BudgetsByZone)
	{
		ALootZoneSpawnManager* ZoneManager = Pair.Key;
		if (!IsValid(ZoneManager) || Pair.Value <= 0)
		{
			continue;
		}

		const uint32 ZoneSeed = HashCombine(static_cast<uint32>(SpawnSeed), static_cast<uint32>(++ZoneIndex));
		ActivatedCount += ZoneManager->ActivateAndSpawnByBudget(Pair.Value, static_cast<int32>(ZoneSeed & 0x7fffffff));
	}

	return ActivatedCount;
}

int32 AGlobalLootSpawnManager::InitializeDataDrivenChests()
{
	if (!HasAuthority() || !GetWorld())
	{
		return 0;
	}

	TArray<AChestSpawnPoint*> GuardedPoints;
	TMap<URandomChestGroup*, TArray<AChestSpawnPoint*>> RandomPointsByGroup;

	for (TActorIterator<AChestSpawnPoint> It(GetWorld()); It; ++It)
	{
		AChestSpawnPoint* Point = *It;
		if (!IsValid(Point) || !Point->CanSpawnDataDrivenChest())
		{
			continue;
		}

		if (Point->GetSpawnMode() == EChestSpawnMode::Guarded)
		{
			GuardedPoints.Add(Point);
		}
		else if (Point->GetSpawnMode() == EChestSpawnMode::Random)
		{
			if (URandomChestGroup* Group = Point->GetRandomGroup())
			{
				RandomPointsByGroup.FindOrAdd(Group).Add(Point);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Data-driven random chest point has no RandomGroup. Point=%s"), *GetNameSafe(Point));
			}
		}
	}

	int32 SpawnedCount = 0;
	for (AChestSpawnPoint* Point : GuardedPoints)
	{
		UChestDefinition* Definition = Point->GetGuardedChestDefinition();
		const uint32 PointSeed = HashCombine(GetTypeHash(SpawnSeed), GetTypeHash(Point->GetFName()));
		if (IsValid(Definition) && IsValid(Point->SpawnConfiguredChest(Definition, static_cast<int32>(PointSeed & 0x7fffffff))))
		{
			++SpawnedCount;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn guarded chest. Point=%s Definition=%s"),
				*GetNameSafe(Point), *GetNameSafe(Definition));
		}
	}

	for (TPair<URandomChestGroup*, TArray<AChestSpawnPoint*>>& Pair : RandomPointsByGroup)
	{
		URandomChestGroup* Group = Pair.Key;
		if (!IsValid(Group) || !IsValid(Group->ChestDefinition) || Group->SpawnCount <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid random chest group. Group=%s Definition=%s SpawnCount=%d"),
				*GetNameSafe(Group), *GetNameSafe(Group ? Group->ChestDefinition : nullptr), Group ? Group->SpawnCount : 0);
			continue;
		}

		TArray<AChestSpawnPoint*> Candidates = Pair.Value;
		const int32 TargetCount = FMath::Min(Group->SpawnCount, Candidates.Num());
		const uint32 GroupSeed = HashCombine(GetTypeHash(SpawnSeed), GetTypeHash(Group->GetFName()));
		FRandomStream RandomStream(static_cast<int32>(GroupSeed & 0x7fffffff));

		for (int32 SpawnIndex = 0; SpawnIndex < TargetCount; ++SpawnIndex)
		{
			AChestSpawnPoint* SelectedPoint = PickWeightedChestPoint(Candidates, RandomStream);
			if (!SelectedPoint)
			{
				break;
			}

			Candidates.RemoveSingleSwap(SelectedPoint);
			const int32 ChestSeed = RandomStream.RandRange(1, MAX_int32);
			if (IsValid(SelectedPoint->SpawnConfiguredChest(Group->ChestDefinition, ChestSeed)))
			{
				++SpawnedCount;
			}
		}
	}

	return SpawnedCount;
}

AChestSpawnPoint* AGlobalLootSpawnManager::PickWeightedChestPoint(
	const TArray<AChestSpawnPoint*>& Candidates,
	FRandomStream& RandomStream)
{
	float TotalWeight = 0.f;
	for (const AChestSpawnPoint* Point : Candidates)
	{
		if (IsValid(Point) && Point->CanSpawnDataDrivenChest())
		{
			TotalWeight += FMath::Max(0.f, Point->GetPointWeight());
		}
	}

	if (TotalWeight <= 0.f)
	{
		return nullptr;
	}

	const float Pick = RandomStream.FRandRange(0.f, TotalWeight);
	float AccumulatedWeight = 0.f;
	for (AChestSpawnPoint* Point : Candidates)
	{
		if (!IsValid(Point) || !Point->CanSpawnDataDrivenChest())
		{
			continue;
		}

		AccumulatedWeight += FMath::Max(0.f, Point->GetPointWeight());
		if (Pick <= AccumulatedWeight)
		{
			return Point;
		}
	}

	return nullptr;
}

TMap<ALootZoneSpawnManager*, int32> AGlobalLootSpawnManager::CalculateZoneBudgets() const
{
	TMap<ALootZoneSpawnManager*, int32> BudgetsByZone;

	const int32 TotalBudget = FMath::Max(0, TotalActivePointBudget);
	int32 FixedBudgetSum = 0;
	float TotalWeight = 0.f;

	for (const FLootZoneBudgetEntry& Entry : ZoneBudgets)
	{
		if (!IsValid(Entry.ZoneManager))
		{
			continue;
		}

		if (Entry.AllocationMode == ELootBudgetAllocationMode::Fixed)
		{
			const int32 Budget = FMath::Max(0, Entry.FixedBudget);
			BudgetsByZone.Add(Entry.ZoneManager, Budget);
			FixedBudgetSum += Budget;
		}
		else
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
			BudgetsByZone.Add(Entry.ZoneManager, 0);
		}
	}

	int32 RemainingBudget = FMath::Max(0, TotalBudget - FixedBudgetSum);
	int32 DistributedBudget = 0;

	if (RemainingBudget > 0 && TotalWeight > 0.f)
	{
		for (const FLootZoneBudgetEntry& Entry : ZoneBudgets)
		{
			if (!IsValid(Entry.ZoneManager) || Entry.AllocationMode != ELootBudgetAllocationMode::Weighted)
			{
				continue;
			}

			const float Weight = FMath::Max(0.f, Entry.Weight);
			const int32 Budget = FMath::FloorToInt((static_cast<float>(RemainingBudget) * Weight) / TotalWeight);
			BudgetsByZone.FindOrAdd(Entry.ZoneManager) += Budget;
			DistributedBudget += Budget;
		}

		int32 Remainder = RemainingBudget - DistributedBudget;
		for (const FLootZoneBudgetEntry& Entry : ZoneBudgets)
		{
			if (Remainder <= 0)
			{
				break;
			}

			if (!IsValid(Entry.ZoneManager) || Entry.AllocationMode != ELootBudgetAllocationMode::Weighted || Entry.Weight <= 0.f)
			{
				continue;
			}

			BudgetsByZone.FindOrAdd(Entry.ZoneManager) += 1;
			--Remainder;
		}
	}

	return BudgetsByZone;
}
