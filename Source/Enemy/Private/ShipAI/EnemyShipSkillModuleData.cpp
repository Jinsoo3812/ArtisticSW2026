#include "ShipAI/EnemyShipSkillModuleData.h"

#include "Abilities/GameplayAbility.h"
#include "Misc/DataValidation.h"
#include "ShipAI/EnemyShipAbilitySet.h"

EDataValidationResult UEnemyShipSkillModuleData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (ModuleId.IsNone())
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Skill Module requires ModuleId.")));
		Result = EDataValidationResult::Invalid;
	}
	if (!AbilitySet)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Skill Module requires an Ability Set.")));
		Result = EDataValidationResult::Invalid;
	}

	FGameplayTagContainer GrantedTags;
	if (AbilitySet)
	{
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilitySet->Abilities)
		{
			if (const UGameplayAbility* AbilityCDO = AbilityClass
				? AbilityClass->GetDefaultObject<UGameplayAbility>()
				: nullptr)
			{
				GrantedTags.AppendTags(AbilityCDO->GetAssetTags());
			}
		}
	}

	TSet<FName> SeenRuleIds;
	TSet<FGameplayTag> SeenTags;
	for (const FEnemyShipSkillRule& Rule : SkillRules)
	{
		if (Rule.RuleId.IsNone() || SeenRuleIds.Contains(Rule.RuleId))
		{
			Context.AddError(FText::FromString(TEXT("Skill Module Rules require unique non-empty RuleId values.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenRuleIds.Add(Rule.RuleId);

		if (!Rule.AbilityTag.IsValid() || SeenTags.Contains(Rule.AbilityTag))
		{
			Context.AddError(FText::FromString(TEXT("Skill Module Rules require unique valid AbilityTag values.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenTags.Add(Rule.AbilityTag);
		if (Rule.AbilityTag.IsValid() && !GrantedTags.HasTagExact(Rule.AbilityTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyShipSkillModule", "MissingAbility", "Rule ability {0} is not granted by the Module Ability Set."),
				FText::FromString(Rule.AbilityTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
