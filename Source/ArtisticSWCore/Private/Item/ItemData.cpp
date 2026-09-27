#include "ItemData.h"
#include "BaseGameplayTags.h"
#include "Engine/Texture2D.h"

int32 UItemData::GetRarityRank(FGameplayTag RarityTag)
{
	if (RarityTag.MatchesTagExact(Item_Rarity_Common))
	{
		return 1;
	}

	if (RarityTag.MatchesTagExact(Item_Rarity_Rare))
	{
		return 3;
	}

	if (RarityTag.MatchesTagExact(Item_Rarity_Epic))
	{
		return 4;
	}

	if (RarityTag.MatchesTagExact(Item_Rarity_Legendary))
	{
		return 5;
	}

	if (RarityTag.MatchesTagExact(Item_Rarity_Relic))
	{
		return 2;
	}

	return 0;
}

bool UItemData::IsRarityAtLeast(FGameplayTag RarityTag, FGameplayTag MinimumRarityTag)
{
	const int32 RarityRank = GetRarityRank(RarityTag);
	const int32 MinimumRarityRank = GetRarityRank(MinimumRarityTag);

	return RarityRank > 0 && MinimumRarityRank > 0 && RarityRank >= MinimumRarityRank;
}
TArray<FGameplayTag> UItemData::GetCraftingMaterialOptions(FGameplayTag ResultItemTag) const
{
	TArray<FGameplayTag> Options;
	const FItemDefinition* Result = FindItemDefinition(ResultItemTag);
	if (!Result) return Options;
	const EItemProgressionKind MaterialKind = Result->ProgressionKind == EItemProgressionKind::Weapon
		? EItemProgressionKind::WeaponMaterial : Result->ProgressionKind == EItemProgressionKind::Consumable
		? EItemProgressionKind::ConsumableMaterial : EItemProgressionKind::None;
	if (MaterialKind == EItemProgressionKind::None) return Options;
	const EItemProgressionKind SpecialKind = MaterialKind == EItemProgressionKind::WeaponMaterial
		? EItemProgressionKind::WeaponSpecialMaterial : EItemProgressionKind::ConsumableSpecialMaterial;
	for (const TPair<FGameplayTag, FItemDefinition>& Pair : ItemDefinitions)
	{
		const bool bSpecial = Pair.Value.ProgressionKind == SpecialKind
			|| Pair.Value.ProgressionKind == EItemProgressionKind::UniversalSpecialMaterial;
		if (bSpecial || (Pair.Value.ProgressionKind == MaterialKind
			&& Pair.Value.ProgressionTier <= Result->ProgressionTier
			&& Pair.Value.ProgressionTier >= Result->ProgressionTier - 1))
		{
			Options.Add(Pair.Key);
		}
	}
	Options.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.ToString() < B.ToString(); });
	return Options;
}
