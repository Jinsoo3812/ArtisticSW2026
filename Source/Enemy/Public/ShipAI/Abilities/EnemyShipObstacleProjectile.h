#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipObstacleProjectile.generated.h"

class AEnemyShipObstacle;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UNiagaraSystem;

/** Collisionless ballistic carrier that converts into an obstacle at its authored air point. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipObstacleProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipObstacleProjectile();

	void InitializeObstacleProjectile(
		const FVector& InLaunchVelocity,
		const FVector& InTargetPoint,
		float InTravelSeconds,
		TSubclassOf<AEnemyShipObstacle> InObstacleClass,
		const FRotator& InObstacleSpawnRotationOffset);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> ProjectileRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Niagara spawned when the carrier reaches its target and becomes an obstacle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle Projectile|Effects")
	TObjectPtr<UNiagaraSystem> ObstacleSpawnEffect;

	/** Uniform world scale applied to ObstacleSpawnEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle Projectile|Effects", meta = (ClampMin = "0.01"))
	float ObstacleSpawnEffectScale = 1.0f;

	/** Niagara simulation speed. 0.5 plays at half speed and lasts roughly twice as long. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle Projectile|Effects", meta = (ClampMin = "0.01"))
	float ObstacleSpawnEffectPlaybackSpeed = 1.0f;

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnObstacleEffect(
		UNiagaraSystem* Effect,
		FVector_NetQuantize Location,
		FRotator Rotation,
		float UniformScale,
		float PlaybackSpeed);

	void ReachTargetAndSpawnObstacle();

	FVector TargetPoint = FVector::ZeroVector;
	FRotator ObstacleSpawnRotationOffset = FRotator::ZeroRotator;
	TSubclassOf<AEnemyShipObstacle> ObstacleClass;
	FTimerHandle ArrivalTimerHandle;
};
