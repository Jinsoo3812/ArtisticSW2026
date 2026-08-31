#include "ShipAI/EnemyShipPatternData.h"

#include "Misc/DataValidation.h"
#include "ShipAI/EnemyShipSkillModuleData.h"

EDataValidationResult UEnemyShipPatternData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FName> SeenModuleIds;
	TSet<FName> SeenRuleIds;
	TSet<FGameplayTag> SeenTags;
	for (const UEnemyShipSkillModuleData* Module : SkillModules)
	{
		if (!Module)
		{
			Context.AddError(FText::FromString(TEXT("Enemy Ship Pattern contains a null Skill Module.")));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Module->ModuleId.IsNone() || SeenModuleIds.Contains(Module->ModuleId))
		{
			Context.AddError(FText::FromString(TEXT("Enemy Ship Pattern contains a missing or duplicate Skill Module ID.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenModuleIds.Add(Module->ModuleId);
		for (const FEnemyShipSkillRule& Rule : Module->SkillRules)
		{
			if (Rule.RuleId.IsNone() || SeenRuleIds.Contains(Rule.RuleId))
			{
				Context.AddError(FText::FromString(TEXT("Composed Skill Rules require unique non-empty RuleId values.")));
				Result = EDataValidationResult::Invalid;
			}
			SeenRuleIds.Add(Rule.RuleId);
			if (Rule.AbilityTag.IsValid() && SeenTags.Contains(Rule.AbilityTag))
			{
				Context.AddError(FText::FromString(TEXT("Composed Skill Modules contain a duplicate AbilityTag.")));
				Result = EDataValidationResult::Invalid;
			}
			SeenTags.Add(Rule.AbilityTag);
			if (SelectionPolicy == EEnemyShipPatternSelectionPolicy::WeightedRandom && Rule.Weight <= 0.0f)
			{
				Context.AddError(FText::FromString(TEXT("WeightedRandom skill rules require Weight > 0.")));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	if (NavigationProfile.DangerCloseDistance > NavigationProfile.IdealDistance)
	{
		Context.AddError(FText::FromString(TEXT("DangerCloseDistance must not exceed IdealDistance.")));
		Result = EDataValidationResult::Invalid;
	}
	if (NavigationProfile.ReturnTriggerDistance < NavigationProfile.ReturnArrivalDistance)
	{
		Context.AddError(FText::FromString(TEXT("ReturnTriggerDistance must be greater than or equal to ReturnArrivalDistance.")));
		Result = EDataValidationResult::Invalid;
	}
	if (NavigationProfile.ZeroHealthCannonCooldownMultiplier < 1.0f)
	{
		Context.AddError(FText::FromString(TEXT("ZeroHealthCannonCooldownMultiplier must be at least 1.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
