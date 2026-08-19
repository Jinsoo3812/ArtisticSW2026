#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GASAttributeDamageExecution.generated.h"

/**
 * Calculates direct weapon damage inside the GameplayEffect execution.
 *
 * Source Strength is snapshotted when the spec is created. Weapon/ability
 * scalars are supplied as SetByCaller values, leaving this class as the one
 * place where additional source or target attributes can be introduced.
 */
UCLASS()
class GASCORE_API UGASAttributeDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UGASAttributeDamageExecution();

	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
