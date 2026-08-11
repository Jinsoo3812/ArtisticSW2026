#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "GA_EnemyShipTimeStop.generated.h"

class ACannon;
class AEnemyShip;
class AEnemyShipTimeStopAimLine;
class AEnemyShipTimeStopField;
class AEnemyShipTimeStopProjectile;
class AShip;

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
	TSubclassOf<AEnemyShipTimeStopProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop")
	TSubclassOf<AEnemyShipTimeStopField> FieldClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning")
	TSubclassOf<AEnemyShipTimeStopAimLine> AimLineClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "0.0", Units = "s"))
	float ChargeDurationSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "1.0", Units = "cm"))
	float AimLineMaximumDistance = 200000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Warning", meta = (ClampMin = "0.01", Units = "s"))
	float AimLineTraceIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aiming", meta = (ClampMin = "0.01", Units = "s"))
	float AimUpdateIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aiming", meta = (ClampMin = "0.0"))
	float ShipTurnResponsiveness = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aiming", meta = (ClampMin = "0.0"))
	float ShipTurnMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Projectile", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 5000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Projectile", meta = (ClampMin = "0.1", Units = "s"))
	float ProjectileLifetimeSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "1.0", Units = "cm"))
	float EffectRadius = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effect", meta = (ClampMin = "0.05", Units = "s"))
	float TimeStopDurationSeconds = 3.0f;

private:
	void FireTimeStopProjectile();
	void UpdateChargeAiming();
	bool IsValidPlayerTarget(const AShip* Candidate) const;

	TWeakObjectPtr<AEnemyShip> ActiveShip;
	TWeakObjectPtr<AShip> ActiveTarget;
	TWeakObjectPtr<ACannon> SelectedCannon;
	TWeakObjectPtr<AEnemyShipTimeStopAimLine> AimLineActor;
	FEnemyShipNavigationOverrideHandle NavigationOverrideHandle;
	FTimerHandle ChargeTimerHandle;
	FTimerHandle AimUpdateTimerHandle;
	FVector FixedLineStart = FVector::ZeroVector;
	FVector FixedLineEnd = FVector::ZeroVector;
	FVector FixedTargetPoint = FVector::ZeroVector;
	FVector FixedLaunchDirection = FVector::ForwardVector;
};
