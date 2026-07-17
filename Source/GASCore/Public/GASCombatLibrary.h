#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GASCombatLibrary.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

UCLASS()
class GASCORE_API UGASCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "GAS|Combat", meta = (AdvancedDisplay = "EffectLevel,bAddHitResult,HitResult", AutoCreateRefTerm = "HitResult"))
	static FGameplayEffectSpecHandle MakeDamageEffectSpec(
		UAbilitySystemComponent* SourceASC,
		TSubclassOf<UGameplayEffect> DamageEffectClass,
		float Damage,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		int32 EffectLevel = 1,
		bool bAddHitResult = false,
		const FHitResult& HitResult = FHitResult());

	UFUNCTION(BlueprintCallable, Category = "GAS|Combat", meta = (AdvancedDisplay = "EffectLevel"))
	static FGameplayEffectSpecHandle MakeHealingEffectSpec(
		UAbilitySystemComponent* SourceASC,
		TSubclassOf<UGameplayEffect> HealingEffectClass,
		float Healing,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		int32 EffectLevel = 1);

};
