#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "GA_EnemyShipTimeStop.generated.h"

class ACannon;
class AEnemyShip;
class AEnemyShipTimeStopAimLine;
class AEnemyShipTimeStopField;
class AShip;
class UNiagaraSystem;

/** Captures one dodgeable straight warning line, then fires an independent time-stop projectile along it. */
UCLASS(Blueprintable)
class ENEMY_API UGA_EnemyShipTimeStop : public UEnemyShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyShipTimeStop();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	static FVector ResolveFixedLineEnd(
		const FVector& LineStart,
		const AShip* TargetShip,
		float MaximumDistance = 200000.0f);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop")
	static FGameplayTag GetTimeStopAbilityTag();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop")
	TSubclassOf<AEnemyShipTimeStopField> FieldClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning")
	TSubclassOf<AEnemyShipTimeStopAimLine> AimLineClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "0.0", Units = "s"))
	float ChargeDurationSeconds = 3.0f;

	/** Time spent charging after the target point has been locked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Charge", meta = (ClampMin = "0.0", Units = "s"))
	float LockedChargeDurationSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Charge")
	TObjectPtr<UNiagaraSystem> ChargingEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Charge", meta = (ClampMin = "0.01"))
	float ChargingEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Charge", meta = (ClampMin = "0.01"))
	float ChargingEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Charge", meta = (ClampMin = "0.01"))
	float ChargingEffectPlaybackSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "1.0", Units = "cm"))
	float AimLineMaximumDistance = 200000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "0.01", Units = "s"))
	float AimLineTraceIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aiming", meta = (ClampMin = "0.01", Units = "s"))
	float AimUpdateIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit")
	TObjectPtr<UNiagaraSystem> InstantHitTrailEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit", meta = (ClampMin = "0.01"))
	float InstantHitTrailEffectScale = 1.0f;

	/** Lifetime passed directly to the instant-hit Niagara's ribbon and electricity particles, in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit",
		meta = (DisplayName = "Trail Lifetime Seconds", ClampMin = "0.01"))
	float InstantHitTrailLifetimeSeconds = 3.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit", meta = (ClampMin = "0.01"))
	float InstantHitTrailPlaybackSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit", meta = (ClampMin = "1.0"))
	float MissDistanceMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Instant Hit",
		meta = (DisplayName = "Presentation Cleanup Delay Seconds", ClampMin = "0.1"))
	float InstantHitPresentationLifetime = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect")
	TObjectPtr<UNiagaraSystem> ExplosionEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "0.01"))
	float ExplosionEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "0.01"))
	float ExplosionEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "0.01"))
	float ExplosionEffectPlaybackSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "1.0", Units = "cm"))
	float EffectRadius = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "0.05", Units = "s"))
	float TimeStopDurationSeconds = 3.0f;

private:
	void ConfirmAimAndBeginCharge();
	void FireInstantHit();
	void UpdateChargeAiming();
	bool IsValidPlayerTarget(const AShip* Candidate) const;

	TWeakObjectPtr<AEnemyShip> ActiveShip;
	TWeakObjectPtr<AShip> ActiveTarget;
	TWeakObjectPtr<ACannon> SelectedCannon;
	TWeakObjectPtr<AEnemyShipTimeStopAimLine> AimLineActor;
	FTimerHandle ChargeTimerHandle;
	FTimerHandle AimUpdateTimerHandle;
	FVector FixedLineStart = FVector::ZeroVector;
	FVector FixedLineEnd = FVector::ZeroVector;
	FVector FixedTargetPoint = FVector::ZeroVector;
	FVector FixedLaunchDirection = FVector::ForwardVector;
	float ConfirmedShotDistance = 0.0f;
	bool bAimLocked = false;
};
