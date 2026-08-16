#include "StatusEffectLibrary.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

FActiveGameplayEffectHandle UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(
	UAbilitySystemComponent* TargetASC,
	const FGameplayEffectSpecHandle& EffectSpecHandle,
	FGameplayTag RefreshGrantedTag)
{
	if (!TargetASC || !EffectSpecHandle.IsValid() || !EffectSpecHandle.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	const FGameplayEffectSpec& EffectSpec = *EffectSpecHandle.Data.Get();
	if (!EffectSpec.Def)
	{
		return FActiveGameplayEffectHandle();
	}

	// The GE class is the default status identity. Removing and reapplying the
	// effect guarantees one active timer and resets both duration and period.
	FGameplayEffectQuery SameEffectClassQuery;
	SameEffectClassQuery.EffectDefinition = EffectSpec.Def->GetClass();
	TargetASC->RemoveActiveEffects(SameEffectClassQuery);

	if (RefreshGrantedTag.IsValid())
	{
		FGameplayTagContainer RefreshGrantedTags;
		RefreshGrantedTags.AddTag(RefreshGrantedTag);
		TargetASC->RemoveActiveEffectsWithGrantedTags(RefreshGrantedTags);
	}

	return TargetASC->ApplyGameplayEffectSpecToSelf(EffectSpec);
}
