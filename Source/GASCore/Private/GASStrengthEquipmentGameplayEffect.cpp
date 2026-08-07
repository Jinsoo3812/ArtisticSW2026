#include "GASStrengthEquipmentGameplayEffect.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

UGASStrengthEquipmentGameplayEffect::UGASStrengthEquipmentGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FGameplayModifierInfo StrengthModifier;
	StrengthModifier.Attribute = UBaseAttributeSet::GetStrengthAttribute();
	StrengthModifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat SetByCallerStrength;
	SetByCallerStrength.DataTag = Data_StrengthBonus;
	StrengthModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerStrength);
	Modifiers.Add(StrengthModifier);
}
