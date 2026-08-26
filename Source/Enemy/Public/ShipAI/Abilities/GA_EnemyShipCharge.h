#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "GA_EnemyShipCharge.generated.h"

class AEnemyShip;
class AEnemyShipChargeTelegraph;
class AShip;
class UGameplayEffect;
class UPrimitiveComponent;

/** Turns toward the selected Player Ship, then charges and damages it on one Physics Root collision. */
UCLASS(Blueprintable)
class ENEMY_API UGA_EnemyShipCharge : public UEnemyShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyShipCharge();

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

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Charge")
	float GetChargePropulsionMultiplier() const { return ChargePropulsionMultiplier; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Charge")
	float GetChargeTurnMultiplier() const { return ChargeTurnMultiplier; }

	/** True after the ship has reached or crossed the fixed charge endpoint. */
	static bool HasReachedChargeEndpoint(
		const FVector& Start,
		const FVector& Direction,
		float Distance,
		const FVector& CurrentLocation,
		float AcceptanceRadius);

protected:
	/** Fixed distance travelled after the aiming phase locks the direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "1.0", Units = "cm"))
	float ChargeDistance = 10000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.0", Units = "cm"))
	float ChargeEndpointAcceptanceRadius = 150.0f;

	/** Emergency cleanup only; zero disables it. Normal completion is distance or Ship collision. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.0", Units = "s"))
	float ChargeFailsafeDurationSeconds = 0.0f;

	/** The charge starts only after the horizontal bow-to-target angle is within this tolerance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Aiming", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float AimAlignmentToleranceDegrees = 5.0f;

	/** Starts the charge from the final facing if the ship cannot align in time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Aiming", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumAimDurationSeconds = 5.0f;

	/** Multiplies the ship's DT/ASC ForwardPropulsionMultiplier at the physics input layer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "1.0"))
	float ChargePropulsionMultiplier = 2.0f;

	/** Multiplies the ship's DT/ASC TurnTorqueMultiplier during both aiming and charging. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.0"))
	float ChargeTurnMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.01", Units = "s"))
	float SteeringUpdateInterval = 1.0f / 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.0"))
	float SteeringResponsiveness = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaximumTurnInput = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Damage", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumDamageApproachSpeed = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Damage", meta = (ClampMin = "0.0"))
	float DamagePerApproachSpeedUnit = 0.05f;

	/** Zero means uncapped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Damage", meta = (ClampMin = "0.0"))
	float MaximumCollisionDamage = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Damage")
	TSubclassOf<UGameplayEffect> DamageGameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Telegraph")
	TSubclassOf<AEnemyShipChargeTelegraph> ChargeTelegraphClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Telegraph", meta = (ClampMin = "1.0", Units = "cm"))
	float ChargeTelegraphWidth = 1000.0f;

	/** Absolute world Z used by the warning strip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Telegraph", meta = (Units = "cm"))
	float ChargeTelegraphWorldZ = 20.0f;

private:
	UFUNCTION()
	void HandlePhysicsRootHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	void UpdateChargeSteering();
	void BeginCharge();
	void FinishAimByTimeout();
	void FinishChargeByTimeout();
	void SpawnChargeTelegraph();
	void UpdateChargeTelegraph();
	void DestroyChargeTelegraph();
	bool IsValidPlayerTarget(const AShip* Candidate) const;

	TWeakObjectPtr<AEnemyShip> ActiveShip;
	TWeakObjectPtr<AShip> ActiveTarget;
	TWeakObjectPtr<AEnemyShipChargeTelegraph> ChargeTelegraphActor;
	FEnemyShipNavigationOverrideHandle NavigationOverrideHandle;
	FTimerHandle SteeringTimerHandle;
	FTimerHandle AimTimeoutTimerHandle;
	FTimerHandle DurationTimerHandle;
	FVector ChargeStartLocation = FVector::ZeroVector;
	FVector ChargeDirection = FVector::ForwardVector;
	bool bPreviousNotifyRigidBodyCollision = false;
	bool bBoundPhysicsHit = false;
	bool bAddedChargingTag = false;
	bool bCollisionConsumed = false;
	bool bChargeStarted = false;
};
