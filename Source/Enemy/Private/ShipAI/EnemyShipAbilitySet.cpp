#include "ShipAI/EnemyShipAbilitySet.h"

#include "Abilities/GameplayAbility.h"
#include "Misc/DataValidation.h"

EDataValidationResult UEnemyShipAbilitySet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<UClass*> SeenClasses;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
	{
		if (!AbilityClass)
		{
			Context.AddError(FText::FromString(TEXT("Enemy Ship Ability Set contains a null class.")));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (SeenClasses.Contains(AbilityClass.Get()))
		{
			Context.AddError(FText::FromString(TEXT("Enemy Ship Ability Set contains a duplicate class.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenClasses.Add(AbilityClass.Get());
	}
	return Result;
}
