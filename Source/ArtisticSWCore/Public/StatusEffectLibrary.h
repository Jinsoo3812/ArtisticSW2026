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
	// Applies duration/periodic damage effects such as poison, burn, or bleed.
	// If RefreshGrantedTag is valid, the existing effect with that granted tag is removed first.
	UFUNCTION(BlueprintCallable, Category = "GAS|Status")
	static FActiveGameplayEffectHandle ApplyDurationDamageEffectSpecToTarget(
		UAbilitySystemComponent* TargetASC,
		const FGameplayEffectSpecHandle& EffectSpecHandle,
		FGameplayTag RefreshGrantedTag);
};
