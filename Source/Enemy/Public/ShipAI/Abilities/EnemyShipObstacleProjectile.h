#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipObstacleProjectile.generated.h"

class AEnemyShipObstacle;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

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
		TSubclassOf<AEnemyShipObstacle> InObstacleClass);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> ProjectileRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	void ReachTargetAndSpawnObstacle();

	FVector TargetPoint = FVector::ZeroVector;
	TSubclassOf<AEnemyShipObstacle> ObstacleClass;
	FTimerHandle ArrivalTimerHandle;
};
