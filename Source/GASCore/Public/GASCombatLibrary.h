#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GASCombatLibrary.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct GASCORE_API FStrengthDamageRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<UAbilitySystemComponent> SourceASC = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float AttackCoefficient = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float ChargeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<AActor> EffectCauser = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	int32 EffectLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bAddHitResult = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	FHitResult HitResult;
};

UCLASS()
class GASCORE_API UGASCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GAS|Combat")
	static float CalculateStrengthDamage(float Strength, float AttackCoefficient, float ChargeMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "GAS|Combat")
	static FGameplayEffectSpecHandle MakeStrengthDamageEffectSpec(const FStrengthDamageRequest& Request);

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
