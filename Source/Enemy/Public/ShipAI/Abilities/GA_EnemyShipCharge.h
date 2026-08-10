#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "GA_EnemyShipCharge.generated.h"

class AEnemyShip;
class AShip;
class UGameplayEffect;
class UPrimitiveComponent;

/** Drives toward the selected Player Ship and damages that ship on one Physics Root collision. */
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

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.05", Units = "s"))
	float ChargeDurationSeconds = 3.0f;

	/** Multiplies the ship's DT/ASC ForwardPropulsionMultiplier at the physics input layer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "1.0"))
	float ChargePropulsionMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge", meta = (ClampMin = "0.01", Units = "s"))
	float SteeringUpdateInterval = 0.05f;

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

private:
	UFUNCTION()
	void HandlePhysicsRootHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	void UpdateChargeSteering();
	void FinishChargeByTimeout();
	bool IsValidPlayerTarget(const AShip* Candidate) const;

	TWeakObjectPtr<AEnemyShip> ActiveShip;
	TWeakObjectPtr<AShip> ActiveTarget;
	FEnemyShipNavigationOverrideHandle NavigationOverrideHandle;
	FTimerHandle SteeringTimerHandle;
	FTimerHandle DurationTimerHandle;
	bool bPreviousNotifyRigidBodyCollision = false;
	bool bBoundPhysicsHit = false;
	bool bAddedChargingTag = false;
	bool bCollisionConsumed = false;
};
