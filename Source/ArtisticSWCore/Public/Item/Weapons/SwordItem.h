#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Item/BaseItem.h"
#include "SwordItem.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class USceneComponent;

/**
 * Player sword item.
 *
 * The item keeps the normal ABaseItem pickup/equipment behavior and owns the
 * server-authoritative trace state used during an attack montage's active
 * frames.
 */
UCLASS()
class ARTISTICSWCORE_API ASwordItem : public ABaseItem
{
	GENERATED_BODY()

	friend class FStrengthMeleePayloadTest;

public:
	ASwordItem();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Sword|Trace")
	bool HitScanStart(const FGameplayEffectSpecHandle& DamageEffectSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "Sword|Trace")
	void HitScanEnd();

	/** Samples the current blade position once during an active animation window. */
	void SampleHitScan();

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	bool IsHitScanActive() const { return bHitScanActive; }

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	USceneComponent* GetTraceStartPoint() const { return TraceStartPoint; }

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	USceneComponent* GetTraceEndPoint() const { return TraceEndPoint; }

	UFUNCTION(BlueprintPure, Category = "Sword|Damage")
	TSubclassOf<UGameplayEffect> GetDamageEffectClass() const { return DamageEffectClass; }

	UFUNCTION(BlueprintPure, Category = "Sword|Damage")
	float GetAttackCoefficient() const { return AttackCoefficient; }

	UFUNCTION(BlueprintPure, Category = "Sword|Damage", meta = (DeprecatedFunction, DeprecationMessage = "Strength attacks use UGASCombatLibrary::CalculateStrengthDamage."))
	float CalculateDamage(float AttackPower) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sword|Trace")
	TObjectPtr<USceneComponent> TraceStartPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sword|Trace")
	TObjectPtr<USceneComponent> TraceEndPoint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace")
	TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjectTypes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace", meta = (ClampMin = "0.1"))
	float TraceRadius = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace")
	bool bDrawDebugTrace = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace")
	bool bIgnoreSameTeam = true;

	/** Optional override. The player ability supplies a native default when this is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Damage", meta = (ClampMin = "0.0"))
	float AttackCoefficient = 1.0f;

	/** Applied after direct damage, once per target in an attack window. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Status")
	TArray<TSubclassOf<UGameplayEffect>> StatusEffectClasses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Damage", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Unused by Strength-based attacks."))
	float BaseDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Damage", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Use AttackCoefficient."))
	float AttackPowerMultiplier = 1.0f;

private:
	void TraceSegment(const FVector& Start, const FVector& End);
	void HandleHit(const FHitResult& HitResult);
	void ApplyEffectToTarget(UAbilitySystemComponent* TargetASC, const FHitResult& HitResult);
	bool ShouldIgnoreActor(const AActor* OtherActor, const UAbilitySystemComponent* TargetASC) const;
	UAbilitySystemComponent* ResolveSourceAbilitySystem() const;
	AActor* ResolveSourceActor() const;
	void ClearHitScanState();
	bool BuildStatusEffectSpecs(UAbilitySystemComponent* SourceASC);

	FGameplayEffectSpecHandle CachedDamageEffectSpecHandle;
	TArray<FGameplayEffectSpecHandle> CachedStatusEffectSpecHandles;
	TSet<TWeakObjectPtr<AActor>> HitActors;
	FVector PreviousTraceStart = FVector::ZeroVector;
	FVector PreviousTraceEnd = FVector::ZeroVector;
	bool bHasPreviousTracePoints = false;
	bool bHitScanActive = false;
};
