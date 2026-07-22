#include "WaterBombEffects.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

UWaterBombAttackSpeedGameplayEffect::UWaterBombAttackSpeedGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(5.0f));

	FSetByCallerFloat SetByCallerMultiplier;
	SetByCallerMultiplier.DataTag = Data_Effect_AttackSpeedMultiplier;

	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UBaseAttributeSet::GetAttackSpeedMultiplierAttribute();
	Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerMultiplier);
}

UWaterBombCannonDisableGameplayEffect::UWaterBombCannonDisableGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(5.0f));
}
