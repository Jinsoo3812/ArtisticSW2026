#include "GAS/EnemyShipWeakeningEffect.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

UEnemyShipWeakeningEffect::UEnemyShipWeakeningEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	auto AddMultiplier = [this](const FGameplayAttribute& Attribute, const FGameplayTag& Tag)
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
		FSetByCallerFloat Magnitude;
		Magnitude.DataTag = Tag;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
		Modifiers.Add(Modifier);
	};
	AddMultiplier(UBaseAttributeSet::GetStrengthAttribute(), Data_Effect_ShipCrewStrengthMultiplier);
	AddMultiplier(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute(), Data_Effect_ShipCrewMoveSpeedMultiplier);
	AddMultiplier(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute(), Data_Effect_ShipCrewAttackSpeedMultiplier);
}
