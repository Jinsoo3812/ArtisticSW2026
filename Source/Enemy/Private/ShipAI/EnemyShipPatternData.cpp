#include "ShipAI/EnemyShipPatternData.h"

#include "Misc/DataValidation.h"

EDataValidationResult UEnemyShipPatternData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FGameplayTag> SeenTags;
	for (const FEnemyShipSkillRule& Rule : SkillRules)
	{
		if (!Rule.AbilityTag.IsValid())
		{
			Context.AddError(FText::FromString(TEXT("Enemy Ship skill rule has no AbilityTag.")));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenTags.Contains(Rule.AbilityTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyShipPattern", "DuplicateAbilityTag", "Duplicate AbilityTag: {0}"),
				FText::FromString(Rule.AbilityTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenTags.Add(Rule.AbilityTag);

		if (Rule.MinimumDistance > Rule.MaximumDistance)
		{
			Context.AddError(FText::FromString(TEXT("Skill rule MinimumDistance exceeds MaximumDistance.")));
			Result = EDataValidationResult::Invalid;
		}
		if (Rule.MinimumHealthRatio > Rule.MaximumHealthRatio)
		{
			Context.AddError(FText::FromString(TEXT("Skill rule MinimumHealthRatio exceeds MaximumHealthRatio.")));
			Result = EDataValidationResult::Invalid;
		}
		if (SelectionPolicy == EEnemyShipPatternSelectionPolicy::WeightedRandom && Rule.Weight <= 0.0f)
		{
			Context.AddError(FText::FromString(TEXT("WeightedRandom skill rules require Weight > 0.")));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (NavigationProfile.DangerCloseDistance > NavigationProfile.IdealDistance)
	{
		Context.AddError(FText::FromString(TEXT("DangerCloseDistance must not exceed IdealDistance.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
