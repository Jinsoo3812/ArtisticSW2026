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
	static FGameplayTag CanonicalStatusTag(FGameplayTag Tag);
	static FGameplayTag ResolveStatusTag(const FGameplayEffectSpec& Spec);
	static bool CanApplyStatus(UAbilitySystemComponent* TargetASC, const FGameplayEffectSpec& Spec, FGameplayTag StatusTag);
	/**
	 * Applies a duration/periodic status effect without stacking duplicate timers.
	 * Existing status timers are preserved; duplicate status identities are rejected.
	 * RefreshGrantedTag is retained as a legacy status identity hint.
	 */
	UFUNCTION(BlueprintCallable, Category = "GAS|Status")
	static FActiveGameplayEffectHandle ApplyDurationDamageEffectSpecToTarget(
		UAbilitySystemComponent* TargetASC,
		const FGameplayEffectSpecHandle& EffectSpecHandle,
		FGameplayTag RefreshGrantedTag);
};
