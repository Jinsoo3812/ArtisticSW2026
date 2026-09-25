#include "ItemSpawn/BossChestGuaranteedLootData.h"

#include "BaseCharacter.h"

void UBossChestGuaranteedLootData::PostLoad()
{
	Super::PostLoad();
	TArray<FStorageItemEntry> Ignored;
	FindItemsForExactClass(nullptr, Ignored);
}

bool UBossChestGuaranteedLootData::FindItemsForExactClass(
	const UClass* BossClass, TArray<FStorageItemEntry>& OutItems) const
{
	OutItems.Reset();
	TSet<const UClass*> SeenClasses;
	const FGuaranteedBossLootEntry* Match = nullptr;
	bool bValid = true;
	for (const FGuaranteedBossLootEntry& Entry : Entries)
	{
		const UClass* EntryClass = Entry.BossClass.LoadSynchronous();
		if (!EntryClass || SeenClasses.Contains(EntryClass))
		{
			UE_LOG(LogTemp, Error, TEXT("Boss loot data %s has a null or duplicate boss class"), *GetNameSafe(this));
			bValid = false;
		}
		else
		{
			SeenClasses.Add(EntryClass);
			if (EntryClass == BossClass) Match = &Entry;
		}
		for (const FStorageItemEntry& Item : Entry.GuaranteedItems)
		{
			if (!Item.ItemTag.IsValid() || Item.Count < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("Boss loot data %s has an invalid item entry"), *GetNameSafe(this));
				bValid = false;
			}
		}
	}
	if (!bValid || !Match) return false;
	OutItems = Match->GuaranteedItems;
	return true;
}
