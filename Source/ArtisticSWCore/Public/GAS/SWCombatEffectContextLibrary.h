#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWCombatEffectContextLibrary.generated.h"

class UAbilitySystemComponent;
struct FGameplayEffectSpec;
struct FSWPathCuePayload;

/** Shared construction and enrichment API for every combat GameplayEffectContext. */
UCLASS()
class ARTISTICSWCORE_API USWCombatEffectContextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "GAS|Combat|Context",
		meta = (AdvancedDisplay = "TargetActor,bAddHitResult,HitResult,ExplicitImpactDirection", AutoCreateRefTerm = "HitResult,ExplicitImpactDirection"))
	static FGameplayEffectContextHandle MakeCombatEffectContext(
		UAbilitySystemComponent* SourceASC,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		AActor* TargetActor = nullptr,
		bool bAddHitResult = false,
		const FHitResult& HitResult = FHitResult(),
		const FVector& ExplicitImpactDirection = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "GAS|Combat|Context")
	static FGameplayEffectContextHandle SetImpactDirection(
		FGameplayEffectContextHandle ContextHandle,
		const FVector& ImpactDirection);

	UFUNCTION(BlueprintPure, Category = "GAS|Combat|Context")
	static bool GetImpactDirection(
		const FGameplayEffectContextHandle& ContextHandle,
		FVector& OutImpactDirection);

	UFUNCTION(BlueprintCallable, Category = "GAS|Combat|Context")
	static FGameplayEffectContextHandle SetPathCuePayload(
		FGameplayEffectContextHandle ContextHandle,
		const FSWPathCuePayload& PathPayload);

	UFUNCTION(BlueprintPure, Category = "GAS|Combat|Context")
	static bool GetPathCuePayload(
		const FGameplayEffectContextHandle& ContextHandle,
		FSWPathCuePayload& OutPathPayload);

	static FVector ResolveImpactDirection(
		const AActor* InstigatorActor,
		const AActor* EffectCauser,
		const AActor* TargetActor,
		const FHitResult* HitResult,
		const FVector& ExplicitImpactDirection = FVector::ZeroVector);

	static bool EnrichCombatEffectContext(
		FGameplayEffectContextHandle& ContextHandle,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		AActor* TargetActor,
		const FHitResult* HitResult = nullptr,
		const FVector& ExplicitImpactDirection = FVector::ZeroVector);

	static bool EnrichCombatEffectSpec(
		FGameplayEffectSpec& EffectSpec,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		AActor* TargetActor,
		const FHitResult* HitResult = nullptr,
		const FVector& ExplicitImpactDirection = FVector::ZeroVector);
};
