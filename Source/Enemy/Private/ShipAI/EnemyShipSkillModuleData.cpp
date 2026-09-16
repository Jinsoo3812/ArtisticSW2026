#include "ShipAI/EnemyShipSkillModuleData.h"

#include "Abilities/GameplayAbility.h"
#include "Misc/DataValidation.h"

FGameplayTag UEnemyShipSkillModuleData::GetAbilityTag() const
{
	const UGameplayAbility* AbilityCDO = AbilityClass
		? AbilityClass->GetDefaultObject<UGameplayAbility>()
		: nullptr;
	if (!AbilityCDO)
	{
		return FGameplayTag();
	}

	const FGameplayTagContainer& AssetTags = AbilityCDO->GetAssetTags();
	for (const FGameplayTag& Tag : AssetTags)
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("GameplayAbility.EnemyShip"), false)))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

EDataValidationResult UEnemyShipSkillModuleData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!AbilityClass)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Skill Module requires an AbilityClass.")));
		Result = EDataValidationResult::Invalid;
	}
	else if (!GetAbilityTag().IsValid())
	{
		Context.AddError(FText::FromString(TEXT("AbilityClass must provide a GameplayAbility.EnemyShip asset tag.")));
		Result = EDataValidationResult::Invalid;
	}
	if (Weight < 0.0f)
	{
		Context.AddError(FText::FromString(TEXT("Skill Module Weight must not be negative.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
