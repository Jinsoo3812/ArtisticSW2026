#include "StatusEffectLibrary.h"

#include "AbilitySystemComponent.h"

FActiveGameplayEffectHandle UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(
	UAbilitySystemComponent* TargetASC,
	const FGameplayEffectSpecHandle& EffectSpecHandle,
	FGameplayTag RefreshGrantedTag)
{
	if (!TargetASC || !EffectSpecHandle.IsValid() || !EffectSpecHandle.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	if (RefreshGrantedTag.IsValid())
	{
		FGameplayTagContainer RefreshGrantedTags;
		RefreshGrantedTags.AddTag(RefreshGrantedTag);
		TargetASC->RemoveActiveEffectsWithGrantedTags(RefreshGrantedTags);
	}

	return TargetASC->ApplyGameplayEffectSpecToSelf(*EffectSpecHandle.Data.Get());
}
