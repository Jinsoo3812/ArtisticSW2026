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

public:
	ASwordItem();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Sword|Trace")
	void HitScanStart(const FGameplayEffectSpecHandle& DamageEffectSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "Sword|Trace")
	void HitScanEnd();

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	bool IsHitScanActive() const { return bHitScanActive; }

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	USceneComponent* GetTraceStartPoint() const { return TraceStartPoint; }

	UFUNCTION(BlueprintPure, Category = "Sword|Trace")
	USceneComponent* GetTraceEndPoint() const { return TraceEndPoint; }

	UFUNCTION(BlueprintPure, Category = "Sword|Damage")
	TSubclassOf<UGameplayEffect> GetDamageEffectClass() const { return DamageEffectClass; }

	UFUNCTION(BlueprintPure, Category = "Sword|Damage")
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Trace", meta = (ClampMin = "0.001"))
	float HitScanInterval = 0.02f;

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
	float BaseDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sword|Damage", meta = (ClampMin = "0.0"))
	float AttackPowerMultiplier = 1.0f;

private:
	void ProcessTrace();
	void TraceSegment(const FVector& Start, const FVector& End);
	void HandleHit(const FHitResult& HitResult);
	void ApplyEffectToTarget(UAbilitySystemComponent* TargetASC, const FHitResult& HitResult);
	bool ShouldIgnoreActor(const AActor* OtherActor, const UAbilitySystemComponent* TargetASC) const;
	UAbilitySystemComponent* ResolveSourceAbilitySystem() const;
	AActor* ResolveSourceActor() const;
	void ClearHitScanState();

	FGameplayEffectSpecHandle CachedDamageEffectSpecHandle;
	FTimerHandle HitScanTimerHandle;
	TSet<TWeakObjectPtr<AActor>> HitActors;
	FVector PreviousTraceStart = FVector::ZeroVector;
	FVector PreviousTraceEnd = FVector::ZeroVector;
	bool bHasPreviousTracePoints = false;
	bool bHitScanActive = false;
};
