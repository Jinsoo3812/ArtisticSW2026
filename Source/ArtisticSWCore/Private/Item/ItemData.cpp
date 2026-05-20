#include "ItemData.h"
#include "Engine/Texture2D.h"

UTexture2D* UItemData::GetIconByTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = FindItemDefinition(ItemTag))
	{
		return Def->Icon2D.LoadSynchronous();
	}

	return nullptr;
}

FText UItemData::GetItemNameByTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = FindItemDefinition(ItemTag))
	{
		return Def->ItemName;
	}

	return FText::FromString(ItemTag.ToString());
}

int32 UItemData::GetMaxStackByTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = FindItemDefinition(ItemTag))
	{
		return Def->MaxStack;
	}

	return -1;
}