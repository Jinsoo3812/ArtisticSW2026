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
