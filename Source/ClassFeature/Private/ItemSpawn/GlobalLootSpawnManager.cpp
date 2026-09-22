#include "ItemSpawn/GlobalLootSpawnManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "Balance/FixedChestDropData.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ItemSpawn/LootZoneSpawnManager.h"
#include "Settings_Item.h"
#include "Item/ItemData.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"
#include "Engine/DataTable.h"
#include "Storage/StorageChest.h"
#include "ItemSpawn/BossChestGuaranteedLootData.h"
#include "BaseCharacter.h"

namespace
{
	using FMaterialDemand = TMap<FGameplayTag, double>;

	bool AddAverageCosts(FMaterialDemand& Demand, const TArray<FCraftingItemStack>& Ingredients,
		const UItemData* Items, EItemProgressionKind ExpectedKind, int32 Tier, double Multiplier)
	{
		bool bValid = true;
		for (const FCraftingItemStack& Ingredient : Ingredients)
		{
			const FItemDefinition* Material = Items->FindItemDefinition(Ingredient.ItemTag);
			const EItemProgressionKind SpecialKind = ExpectedKind == EItemProgressionKind::WeaponMaterial
				? EItemProgressionKind::WeaponSpecialMaterial : ExpectedKind == EItemProgressionKind::ConsumableMaterial
				? EItemProgressionKind::ConsumableSpecialMaterial : EItemProgressionKind::ShipSpecialMaterial;
			const bool bSpecial = Material && (Material->ProgressionKind == SpecialKind
				|| Material->ProgressionKind == EItemProgressionKind::UniversalSpecialMaterial);
			if (!Material || Ingredient.Quantity <= 0
				|| (Material->ProgressionKind != ExpectedKind && !bSpecial)
				|| (!bSpecial
					&& (Material->ProgressionTier > Tier || Material->ProgressionTier < Tier - 1)))
			{
				UE_LOG(LogTemp, Error, TEXT("Invalid progression ingredient %s for tier %d"), *Ingredient.ItemTag.ToString(), Tier);
				bValid = false;
				continue;
			}
			// Special ingredients use independent fixed-chance drops, never the
			// progression expected-value pool derived from chest census.
			if (!bSpecial)
			{
				Demand.FindOrAdd(Ingredient.ItemTag) += Ingredient.Quantity * Multiplier;
			}
		}
		return bValid;
	}

	bool BuildDemand(EProgressionZone Zone, const FProgressionZoneTarget& Target,
		const UItemData* Items, const UDataTable* Recipes, const UShipUpgradeTreeDataAsset* Tree,
		FMaterialDemand& Demand)
	{
		bool bValid = true;
		const int32 Tier = static_cast<int32>(Zone) + 1;
		for (const EItemProgressionKind Kind : {EItemProgressionKind::Weapon, EItemProgressionKind::Consumable})
		{
			TArray<const FCraftingRecipeRow*> Matches;
			TSet<FGameplayTag> SeenResults;
			for (const FName RowName : Recipes->GetRowNames())
			{
				const FCraftingRecipeRow* Recipe = Recipes->FindRow<FCraftingRecipeRow>(RowName, TEXT("Chest progression"), false);
				const FItemDefinition* Result = Recipe ? Items->FindItemDefinition(Recipe->ResultItemTag) : nullptr;
				if (Recipe && Recipe->bEnabled && Result && Result->ProgressionTier == Tier && Result->ProgressionKind == Kind)
				{
					if (SeenResults.Contains(Recipe->ResultItemTag))
					{
						UE_LOG(LogTemp, Error, TEXT("Duplicate enabled progression recipe for %s"), *Recipe->ResultItemTag.ToString());
						bValid = false;
						continue;
					}
					SeenResults.Add(Recipe->ResultItemTag);
					Matches.Add(Recipe);
				}
			}
			const int32 Crafts = Kind == EItemProgressionKind::Weapon ? Target.WeaponCrafts : Target.ConsumableCrafts;
			if (Crafts == 0) continue;
			if (Crafts > 0 && Matches.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("No tier %d recipes for progression kind %d"), Tier, static_cast<int32>(Kind));
				bValid = false;
			}
			for (const FCraftingRecipeRow* Recipe : Matches)
			{
				const double Multiplier = static_cast<double>(Crafts) /
					(Matches.Num() * FMath::Max(1, Recipe->ResultQuantity));
				bValid &= AddAverageCosts(Demand, Recipe->Ingredients, Items,
					Kind == EItemProgressionKind::Weapon ? EItemProgressionKind::WeaponMaterial : EItemProgressionKind::ConsumableMaterial,
					Tier, Multiplier);
			}
		}
		if (Target.ShipUpgrades > 0 && Tree)
		{
			TArray<const FShipUpgradeNodeDefinition*> Nodes;
			for (const FShipUpgradeNodeDefinition& Node : Tree->Nodes)
			{
				if (Node.StatTrack != EShipUpgradeStatTrack::LegacyModifiers && Node.TrackLevel == Tier)
				{
					Nodes.Add(&Node);
				}
			}
			if (Nodes.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("No tier %d ship upgrade nodes"), Tier);
				bValid = false;
			}
			for (const FShipUpgradeNodeDefinition* Node : Nodes)
			{
				bValid &= AddAverageCosts(Demand, Node->ActivationCosts, Items, EItemProgressionKind::ShipMaterial,
					Tier, static_cast<double>(Target.ShipUpgrades) / Nodes.Num());
			}
		}
		else if (Target.ShipUpgrades > 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Ship upgrade tree is missing for tier %d progression"), Tier);
			bValid = false;
		}
		return bValid;
	}
}

AGlobalLootSpawnManager::AGlobalLootSpawnManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void AGlobalLootSpawnManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (bUseRandomSeed)
		{
			SpawnSeed = FMath::Rand();
		}

	if (bInitializeOnBeginPlay)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[this]()
			{
				// Ship child actors receive their settings during ship BeginPlay.
				InitializeLevelLoot();
				GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
					[this]() { RebalanceSpawnedChests(); }));
			}));
	}
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
	return InitializeDataDrivenChestsWithBalance(UProgressionBalanceData::LoadConfigured());
}

int32 AGlobalLootSpawnManager::InitializeDataDrivenChestsWithBalance(const UProgressionBalanceData* Balance)
{
	if (!HasAuthority() || !GetWorld())
	{
		return 0;
	}

	TArray<AChestSpawnPoint*> GuardedPoints;
	TMap<EProgressionZone, TArray<AChestSpawnPoint*>> OceanPointsByZone;
	TMap<EProgressionZone, TArray<AChestSpawnPoint*>> IslandPointsByZone;

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
			if (Point->GetProgressionKind() == EProgressionChestKind::OceanRandom)
			{
				OceanPointsByZone.FindOrAdd(Point->GetProgressionZone()).Add(Point);
			}
			else if (Point->GetProgressionKind() == EProgressionChestKind::IslandRandom)
			{
				IslandPointsByZone.FindOrAdd(Point->GetProgressionZone()).Add(Point);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Random chest point must use OceanRandom or IslandRandom. Point=%s"), *GetNameSafe(Point));
			}
		}
	}

	int32 SpawnedCount = 0;
	for (AChestSpawnPoint* Point : GuardedPoints)
	{
		const uint32 PointSeed = HashCombine(GetTypeHash(SpawnSeed), GetTypeHash(Point->GetFName()));
		if (IsValid(Point->SpawnConfiguredChest(nullptr, static_cast<int32>(PointSeed & 0x7fffffff))))
		{
			++SpawnedCount;
			EnsureBossGuaranteedLoot(Point);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn guarded chest. Point=%s"), *GetNameSafe(Point));
		}
	}

	if (!Balance && (OceanPointsByZone.Num() > 0 || IslandPointsByZone.Num() > 0))
	{
		UE_LOG(LogTemp, Error, TEXT("Random chest activation requires ProgressionBalanceData."));
	}
	for (int32 ZoneIndex = 0; Balance && ZoneIndex < 4; ++ZoneIndex)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(ZoneIndex);
		for (int32 KindIndex = 0; KindIndex < 2; ++KindIndex)
		{
			const EProgressionChestKind Kind = KindIndex == 0 ? EProgressionChestKind::OceanRandom : EProgressionChestKind::IslandRandom;
			const TMap<EProgressionZone, TArray<AChestSpawnPoint*>>& PointsByZone = KindIndex == 0 ? OceanPointsByZone : IslandPointsByZone;
			const TArray<AChestSpawnPoint*>* PlacedPoints = PointsByZone.Find(Zone);
			TArray<AChestSpawnPoint*> Candidates = PlacedPoints ? *PlacedPoints : TArray<AChestSpawnPoint*>();
			const int32 RequestedCount = FMath::Max(0, Balance->GetActiveCount(Zone, Kind));
			if (Candidates.Num() < RequestedCount)
			{
				UE_LOG(LogTemp, Warning, TEXT("Random chest shortage: Zone=%d Kind=%d Placed=%d Requested=%d"),
					ZoneIndex, static_cast<int32>(Kind), Candidates.Num(), RequestedCount);
			}
			const int32 TargetCount = FMath::Min(RequestedCount, Candidates.Num());
			const uint32 GroupSeed = HashCombine(GetTypeHash(SpawnSeed), HashCombine(GetTypeHash(ZoneIndex), GetTypeHash(KindIndex)));
			FRandomStream RandomStream(static_cast<int32>(GroupSeed & 0x7fffffff));
			int32 GroupSpawned = 0;
			for (int32 SpawnIndex = 0; SpawnIndex < TargetCount; ++SpawnIndex)
			{
				AChestSpawnPoint* SelectedPoint = PickWeightedChestPoint(Candidates, RandomStream);
				if (!SelectedPoint) break;
				Candidates.RemoveSingleSwap(SelectedPoint);
				if (IsValid(SelectedPoint->SpawnConfiguredChest(nullptr, RandomStream.RandRange(1, MAX_int32))))
				{
					++GroupSpawned;
					++SpawnedCount;
				}
			}
			UE_LOG(LogTemp, Log, TEXT("Random chests: Zone=%d Kind=%d Requested=%d Placed=%d Spawned=%d"),
				ZoneIndex, static_cast<int32>(Kind), RequestedCount, PlacedPoints ? PlacedPoints->Num() : 0, GroupSpawned);
		}
	}

	return SpawnedCount;
}

bool AGlobalLootSpawnManager::RebalanceSpawnedChests()
{
	const UProgressionBalanceData* Balance = UProgressionBalanceData::LoadConfigured();
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	const UItemData* Items = Settings ? Settings->ItemAssetRegistry.LoadSynchronous() : nullptr;
	const UDataTable* Recipes = Settings ? Settings->CraftingRecipeDataTable.LoadSynchronous() : nullptr;
	const UShipUpgradeTreeDataAsset* Tree = ShipUpgradeTree.LoadSynchronous();
	if (!Tree)
	{
		Tree = LoadObject<UShipUpgradeTreeDataAsset>(nullptr,
			TEXT("/Game/Blueprints/Item/Data/ShipUpgrade/DA_ShipUpgradeTree.DA_ShipUpgradeTree"));
	}
	return RebalanceSpawnedChestsWithData(Balance, Items, Recipes, Tree);
}

int32 AGlobalLootSpawnManager::GetLastActiveChestCount(EProgressionZone Zone) const
{
	const int32 Index = static_cast<int32>(Zone);
	return Index >= 0 && Index < 4 ? LastActiveChestCounts[Index] : 0;
}

bool AGlobalLootSpawnManager::GetSunkChestDrops(EProgressionZone Zone, TArray<FProgressionComputedDrop>& OutDrops) const
{
	OutDrops.Reset();
	const int32 Index = static_cast<int32>(Zone);
	if (!bProgressionFinalized || Index < 0 || Index >= 4 || LastActiveChestCounts[Index] <= 0) return false;
	const UProgressionBalanceData* Balance = UProgressionBalanceData::LoadConfigured();
	if (!Balance) return false;
	const float Ratio = FMath::Clamp(Balance->SunkChestExpectedValueRatio, 0.f, 1.f);
	OutDrops = LastZoneDrops[Index];
	for (FProgressionComputedDrop& Drop : OutDrops) Drop.Chance *= Ratio;
	return true;
}

bool AGlobalLootSpawnManager::RebalanceSpawnedChestsWithData(const UProgressionBalanceData* Balance,
	const UItemData* Items, const UDataTable* Recipes, const UShipUpgradeTreeDataAsset* Tree)
{
	if (!HasAuthority() || !GetWorld() || bProgressionFinalized) return false;
	if (!Balance || !Items || !Recipes)
	{
		UE_LOG(LogTemp, Error, TEXT("Chest progression requires Balance, ItemAssetRegistry and CraftingRecipeDataTable."));
		return false;
	}
	const USettings_Item* ItemSettings = GetDefault<USettings_Item>();
	const UFixedChestDropData* FixedDrops = ItemSettings
		? ItemSettings->FixedChestDropData.LoadSynchronous() : nullptr;
	if (!FixedDrops)
	{
		UE_LOG(LogTemp, Warning, TEXT("Fixed chest drop data is not configured; progression loot remains active."));
	}
	TArray<AChestSpawnPoint*> SpawnedPoints;
	int32 Counts[4] = {0, 0, 0, 0};
	int32 CountsByKind[4][4] = {};
	for (TActorIterator<AChestSpawnPoint> It(GetWorld()); It; ++It)
	{
		AChestSpawnPoint* Point = *It;
		if (!IsValid(Point) || !IsValid(Cast<AStorageChest>(Point->GetSpawnedActor()))) continue;
		const int32 ZoneIndex = static_cast<int32>(Point->GetProgressionZone());
		if (ZoneIndex < 0 || ZoneIndex >= 4) continue;
		const EProgressionChestKind Kind = Point->GetProgressionKind();
		const bool bRandomKind = Kind == EProgressionChestKind::OceanRandom || Kind == EProgressionChestKind::IslandRandom;
		if (bRandomKind != (Point->GetSpawnMode() == EChestSpawnMode::Random))
		{
			UE_LOG(LogTemp, Error, TEXT("Chest point %s has mismatched SpawnMode and ProgressionKind"), *GetNameSafe(Point));
			return false;
		}
		SpawnedPoints.Add(Point);
		++Counts[ZoneIndex];
		const int32 KindIndex = static_cast<int32>(Point->GetProgressionKind());
		if (KindIndex >= 0 && KindIndex < 4) ++CountsByKind[ZoneIndex][KindIndex];
	}
	TArray<FProgressionComputedDrop> DropsByZone[4];
	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(ZoneIndex);
		const FProgressionZoneTarget* Target = Balance->FindTarget(Zone);
		if (Counts[ZoneIndex] == 0) continue;
		UE_LOG(LogTemp, Log, TEXT("Chest census zone %d: ship=%d islandGuard=%d ocean=%d land=%d"),
			ZoneIndex, CountsByKind[ZoneIndex][0], CountsByKind[ZoneIndex][1],
			CountsByKind[ZoneIndex][2], CountsByKind[ZoneIndex][3]);
		if (!Target || Target->FullClears < 1)
		{
			UE_LOG(LogTemp, Error, TEXT("Missing progression target for zone %d"), ZoneIndex);
			return false;
		}
		if (!CalculateZoneDrops(Zone, *Target, Counts[ZoneIndex], Items, Recipes, Tree, DropsByZone[ZoneIndex]))
		{
			return false;
		}
		for (const FProgressionComputedDrop& Drop : DropsByZone[ZoneIndex])
		{
			UE_LOG(LogTemp, Log, TEXT("Progression chest drop: zone=%d item=%s chance=%.4f count=%d"),
				ZoneIndex, *Drop.ItemTag.ToString(), Drop.Chance, Drop.MinCount);
		}
	}
	for (AChestSpawnPoint* Point : SpawnedPoints)
	{
		AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
		const int32 ZoneIndex = static_cast<int32>(Point->GetProgressionZone());
		const uint32 Seed = HashCombine(GetTypeHash(SpawnSeed), GetTypeHash(Point->GetFName()));
		Chest->ReplaceProgressionLoot(DropsByZone[ZoneIndex], Items, static_cast<int32>(Seed & 0x7fffffff));
		if (FixedDrops)
		{
			Point->ApplyFixedChanceDrops(FixedDrops,
				static_cast<int32>(HashCombine(Seed, 0xC32D91A7u) & 0x7fffffffu));
		}
		EnsureBossGuaranteedLoot(Point);
	}
	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		LastActiveChestCounts[ZoneIndex] = Counts[ZoneIndex];
		LastZoneDrops[ZoneIndex] = MoveTemp(DropsByZone[ZoneIndex]);
	}
	bProgressionFinalized = true;
	return true;
}

void AGlobalLootSpawnManager::EnsureBossGuaranteedLoot(AChestSpawnPoint* Point)
{
	if (!HasAuthority() || !IsValid(Point) || Point->GetSpawnMode() != EChestSpawnMode::Guarded
		|| !Point->IsActivated()) return;
	ABaseCharacter* Boss = Point->GetRegisteredBossGuard();
	AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
	if (!IsValid(Boss) || !IsValid(Chest) || Chest->HasBeenOpened()) return;
	UBossChestGuaranteedLootData* Data = BossGuaranteedLootData.LoadSynchronous();
	if (!Data)
	{
		UE_LOG(LogTemp, Warning, TEXT("Boss guaranteed loot data is not configured on %s"), *GetNameSafe(this));
		return;
	}
	TArray<FStorageItemEntry> Items;
	if (Data->FindItemsForExactClass(Boss->GetClass(), Items)) Chest->EnsureGuaranteedLoot(Items);
}

bool AGlobalLootSpawnManager::CalculateZoneDrops(EProgressionZone Zone, const FProgressionZoneTarget& Target,
	int32 ActiveChestCount, const UItemData* Items, const UDataTable* Recipes,
	const UShipUpgradeTreeDataAsset* Tree, TArray<FProgressionComputedDrop>& OutDrops)
{
	OutDrops.Reset();
	if (ActiveChestCount <= 0 || Target.FullClears <= 0 || !Items || !Recipes
		|| (Target.ShipUpgrades > 0 && !Tree)) return false;
	FMaterialDemand Demand;
	if (!BuildDemand(Zone, Target, Items, Recipes, Tree, Demand)) return false;
	if (Demand.IsEmpty() && (Target.WeaponCrafts > 0 || Target.ConsumableCrafts > 0 || Target.ShipUpgrades > 0))
	{
		return false;
	}
	for (const TPair<FGameplayTag, double>& Pair : Demand)
	{
		const double ExpectedPerChest = Pair.Value / (Target.FullClears * ActiveChestCount);
		if (ExpectedPerChest <= 0.0) continue;
		FProgressionComputedDrop& Drop = OutDrops.AddDefaulted_GetRef();
		Drop.ItemTag = Pair.Key;
		Drop.MinCount = FMath::Max(1, FMath::CeilToInt(ExpectedPerChest / .85));
		Drop.MaxCount = Drop.MinCount;
		Drop.Chance = static_cast<float>(ExpectedPerChest / Drop.MinCount);
	}
	OutDrops.Sort([](const FProgressionComputedDrop& A, const FProgressionComputedDrop& B)
	{
		return A.ItemTag.ToString() < B.ItemTag.ToString();
	});
	return true;
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
