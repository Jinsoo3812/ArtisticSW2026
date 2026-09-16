#include "Repair/ShipLeakDamageGameplayEffect.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

UShipLeakDamageGameplayEffect::UShipLeakDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UBaseAttributeSet::GetDamageAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat SetByCallerDamage;
	SetByCallerDamage.DataTag = Data_Damage;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerDamage);
	Modifiers.Add(DamageModifier);
}
