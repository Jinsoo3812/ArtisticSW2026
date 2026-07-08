#include "ItemSpawn/GlobalLootSpawnManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "ItemSpawn/LootZoneSpawnManager.h"

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
			Entry.Weight = ZoneManager->GetBudgetWeight();
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

	if (ZoneBudgets.Num() == 0 || bAutoDiscoverZoneManagers)
	{
		BuildZoneManagerList();
	}

	const TMap<ALootZoneSpawnManager*, int32> BudgetsByZone = CalculateZoneBudgets();
	int32 ActivatedCount = 0;
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
