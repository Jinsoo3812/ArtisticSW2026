#include "GAS/SWAbilitySystemGlobals.h"

#include "GAS/SWGameplayEffectContext.h"

FGameplayEffectContext* USWAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FSWGameplayEffectContext();
}
