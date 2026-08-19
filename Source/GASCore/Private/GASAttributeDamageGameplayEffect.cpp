#include "GASAttributeDamageGameplayEffect.h"

#include "GASAttributeDamageExecution.h"

UGASAttributeDamageGameplayEffect::UGASAttributeDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition DamageExecution;
	DamageExecution.CalculationClass = UGASAttributeDamageExecution::StaticClass();
	Executions.Add(DamageExecution);
}
