#include "Equipment/WeaponAnimationDataAsset.h"

const FWeaponAnimationEntry* UWeaponAnimationDataAsset::FindEntryForTag(const FGameplayTag& ItemTag) const
{
	if (!ItemTag.IsValid())
	{
		return nullptr;
	}

	for (const FWeaponAnimationEntry& Entry : Entries)
	{
		if (Entry.WeaponTag.IsValid() && ItemTag.MatchesTag(Entry.WeaponTag))
		{
			return &Entry;
		}
	}

	return &DefaultEntry;
}
