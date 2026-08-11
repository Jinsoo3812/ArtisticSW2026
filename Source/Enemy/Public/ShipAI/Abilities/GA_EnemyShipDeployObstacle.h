#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "GA_EnemyShipDeployObstacle.generated.h"

class ACannon;
class AEnemyShip;
class AEnemyShipObstacle;
class AEnemyShipObstacleProjectile;
class AShip;

/** Fires a collisionless ballistic carrier and creates a vertical-dropping obstacle in the air. */
UCLASS(Blueprintable)
class ENEMY_API UGA_EnemyShipDeployObstacle : public UEnemyShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyShipDeployObstacle();

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Obstacle")
	static FGameplayTag GetDeployObstacleAbilityTag();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	static FVector CalculateTargetPoint(
		const FVector& EnemyShipLocation,
		const FVector& PlayerShipLocation,
		float LineAlpha,
		float TargetWorldZ);

	static bool CalculateBallisticLaunchVelocity(
		const FVector& Start,
		const FVector& Target,
		float Speed,
		float GravityZ,
		bool bHighArc,
		FVector& OutVelocity,
		float& OutTravelSeconds);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle")
	TSubclassOf<AEnemyShipObstacleProjectile> ObstacleProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle")
	TSubclassOf<AEnemyShipObstacle> ObstacleClass;

	/** XY internal division of the Enemy-to-Player segment: 0 = Enemy, 1 = Player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Target", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float TargetLineAlpha = 0.5f;

	/** Absolute world-space Z of the airborne conversion point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Target", meta = (DisplayName = "Target World Z", Units = "cm"))
	float TargetWorldZ = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Trajectory", meta = (ClampMin = "0.01"))
	float ProjectileSpeedMultiplier = 1.0f;

	/** Low arc is the practical default; enable this for a tall lob. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Trajectory")
	bool bUseHighArc = false;
};
