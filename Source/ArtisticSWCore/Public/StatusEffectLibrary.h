#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StatusEffectLibrary.generated.h"

class UAbilitySystemComponent;

UCLASS()
class ARTISTICSWCORE_API UStatusEffectLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Applies a duration/periodic status effect without stacking duplicate timers.
	 * An active effect with the same GE class is always removed before reapplication,
	 * which resets both duration and periodic timing. RefreshGrantedTag optionally
	 * makes different GE classes mutually exclusive within the same status group.
	 */
	UFUNCTION(BlueprintCallable, Category = "GAS|Status")
	static FActiveGameplayEffectHandle ApplyDurationDamageEffectSpecToTarget(
		UAbilitySystemComponent* TargetASC,
		const FGameplayEffectSpecHandle& EffectSpecHandle,
		FGameplayTag RefreshGrantedTag);
};
