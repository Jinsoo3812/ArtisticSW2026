#include "Effects/AreaSlowGameplayEffect.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

UAreaSlowGameplayEffect::UAreaSlowGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.0f));

	// Repeated hits refresh one target-owned effect instead of multiplying the
	// movement/attack penalty. Both attributes therefore share one authoritative
	// lifetime and can never be restored at different times.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;

	FSetByCallerFloat SetByCallerMoveMultiplier;
	SetByCallerMoveMultiplier.DataTag = Data_Effect_MoveSpeedMultiplier;

	FGameplayModifierInfo& MoveModifier = Modifiers.AddDefaulted_GetRef();
	MoveModifier.Attribute = UBaseAttributeSet::GetMoveSpeedMultiplierAttribute();
	MoveModifier.ModifierOp = EGameplayModOp::Multiplicitive;
	MoveModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerMoveMultiplier);

	FSetByCallerFloat SetByCallerAttackMultiplier;
	SetByCallerAttackMultiplier.DataTag = Data_Effect_AttackSpeedMultiplier;

	FGameplayModifierInfo& AttackModifier = Modifiers.AddDefaulted_GetRef();
	AttackModifier.Attribute = UBaseAttributeSet::GetAttackSpeedMultiplierAttribute();
	AttackModifier.ModifierOp = EGameplayModOp::Multiplicitive;
	AttackModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerAttackMultiplier);
}
