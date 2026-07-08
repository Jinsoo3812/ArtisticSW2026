#include "ItemSpawn/LootSpawnPoint.h"

#include "Engine/World.h"
#include "Item/BaseItem.h"
#include "Storage/StorageChest.h"

ALootSpawnPointBase::ALootSpawnPointBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void ALootSpawnPointBase::ResetSpawnPoint(bool bDestroySpawnedActor)
{
	if (bDestroySpawnedActor && IsValid(SpawnedActor))
	{
		SpawnedActor->Destroy();
	}

	SpawnedActor = nullptr;
	bActivated = false;
}

bool ALootSpawnPointBase::CanBeActivated() const
{
	return bEnabled && !bActivated && PointWeight > 0.f;
}

void ALootSpawnPointBase::MarkActivated(AActor* InSpawnedActor)
{
	SpawnedActor = InSpawnedActor;
	bActivated = true;
}

ABaseItem* ALooseLootSpawnPoint::SpawnLooseLoot(const FZoneLootItemRow& LootRow, TSubclassOf<ABaseItem> FallbackItemClass)
{
	if (!HasAuthority() || !CanBeActivated() || !LootRow.ItemTag.IsValid())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<ABaseItem> ItemClass = LootRow.ItemClassOverride;
	if (!ItemClass)
	{
		ItemClass = ItemClassOverride;
	}
	if (!ItemClass)
	{
		ItemClass = FallbackItemClass;
	}
	if (!ItemClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ABaseItem* SpawnedItem = World->SpawnActorDeferred<ABaseItem>(
		ItemClass,
		GetActorTransform(),
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
	);

	if (!IsValid(SpawnedItem))
	{
		return nullptr;
	}

	SpawnedItem->ItemTag = LootRow.ItemTag;
	SpawnedItem->FinishSpawning(GetActorTransform());
	MarkActivated(SpawnedItem);

	return SpawnedItem;
}

AStorageChest* AChestSpawnPoint::SpawnChest(const TArray<FChestInitialLootRow>& LootRows, TSubclassOf<AStorageChest> FallbackChestClass, int32 Seed)
{
	if (!HasAuthority() || !CanBeActivated())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<AStorageChest> ChestClass = ChestClassOverride ? ChestClassOverride : FallbackChestClass;
	if (!ChestClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AStorageChest* SpawnedChest = World->SpawnActor<AStorageChest>(
		ChestClass,
		GetActorTransform(),
		SpawnParameters
	);

	if (!IsValid(SpawnedChest))
	{
		return nullptr;
	}

	SpawnedChest->ConfigureStorage(SlotCount, ColumnCount, BuildInitialItems(LootRows, Seed));
	MarkActivated(SpawnedChest);

	return SpawnedChest;
}

TArray<FStorageItemEntry> AChestSpawnPoint::BuildInitialItems(const TArray<FChestInitialLootRow>& LootRows, int32 Seed) const
{
	TArray<FStorageItemEntry> Items;
	if (InitialItemRollCount <= 0 || LootRows.Num() <= 0)
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
	for (int32 RollIndex = 0; RollIndex < InitialItemRollCount; ++RollIndex)
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

			const int32 MinCount = FMath::Max(1, Row.MinCount);
			const int32 MaxCount = FMath::Max(MinCount, Row.MaxCount);

			FStorageItemEntry Entry;
			Entry.ItemTag = Row.ItemTag;
			Entry.Count = RandomStream.RandRange(MinCount, MaxCount);
			Items.Add(Entry);
			break;
		}
	}

	return Items;
}
