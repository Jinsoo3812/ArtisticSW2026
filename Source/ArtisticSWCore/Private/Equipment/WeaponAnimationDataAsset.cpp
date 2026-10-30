#include "Equipment/WeaponAnimationDataAsset.h"

UWeaponAnimationDataAsset* UWeaponAnimationDataAsset::CreateSpeedVariant(UObject* Outer, float BasicAttackRate, float AimCycleRate) const
{
	UWeaponAnimationDataAsset* Variant = DuplicateObject<UWeaponAnimationDataAsset>(this, Outer,
		MakeUniqueObjectName(Outer, StaticClass()));
	Variant->ClearFlags(RF_Public | RF_Standalone);
	Variant->SetFlags(RF_Transient);
	auto ApplySpeed = [BasicAttackRate, AimCycleRate](FWeaponAnimationEntry& Entry)
	{
		Entry.BasicAttackPlayRate = BasicAttackRate;
		Entry.AimCyclePlayRate = AimCycleRate;
	};
	ApplySpeed(Variant->DefaultEntry);
	for (FWeaponAnimationEntry& Entry : Variant->Entries) ApplySpeed(Entry);
	return Variant;
}

static bool DoesWeaponGameplayTagMatch(const FGameplayTag& ItemTag, const FGameplayTag& ConfiguredTag)
{
	if (!ItemTag.IsValid() || !ConfiguredTag.IsValid())
	{
		return false;
	}

	if (ItemTag.MatchesTag(ConfiguredTag) || ConfiguredTag.MatchesTag(ItemTag))
	{
		return true;
	}

	// Normalize between "Item.Id.Weapon." and "Item.Weapon."
	FString ItemTagStr = ItemTag.ToString();
	FString ConfigTagStr = ConfiguredTag.ToString();
	ItemTagStr.ReplaceInline(TEXT("Item.Id.Weapon."), TEXT("Item.Weapon."));
	ConfigTagStr.ReplaceInline(TEXT("Item.Id.Weapon."), TEXT("Item.Weapon."));

	if (ItemTagStr.StartsWith(ConfigTagStr) || ConfigTagStr.StartsWith(ItemTagStr))
	{
		return true;
	}

	return false;
}

const FWeaponAnimationEntry* UWeaponAnimationDataAsset::FindEntryForTag(const FGameplayTag& ItemTag) const
{
	if (!ItemTag.IsValid())
	{
		return nullptr;
	}

	for (const FWeaponAnimationEntry& Entry : Entries)
	{
		if (Entry.WeaponTag.IsValid() && DoesWeaponGameplayTagMatch(ItemTag, Entry.WeaponTag))
		{
			return &Entry;
		}
	}

	return &DefaultEntry;
}

