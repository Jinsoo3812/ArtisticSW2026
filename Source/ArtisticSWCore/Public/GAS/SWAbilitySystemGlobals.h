#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"
#include "SWAbilitySystemGlobals.generated.h"

/** Allocates the project gameplay-effect context used by every ASC. */
UCLASS()
class ARTISTICSWCORE_API USWAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
