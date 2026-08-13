#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipTimeStopProjectile.generated.h"

class AEnemyShipTimeStopField;
class AEnemyShip;
class AShip;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Independent straight-line skill projectile; intentionally does not derive from ACannonball. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTimeStopProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipTimeStopProjectile();

	void InitializeTimeStopProjectile(
		AEnemyShip* InSourceShip,
		const FVector& LaunchDirection,
		float Speed,
		float InLifetimeSeconds,
		float InEffectRadius,
		float InEffectDurationSeconds,
		TSubclassOf<AEnemyShipTimeStopField> InFieldClass);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	UFUNCTION()
	void OnProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<AEnemyShip> SourceShip;

	UPROPERTY()
	TSubclassOf<AEnemyShipTimeStopField> FieldClass;

	float EffectRadius = 1500.0f;
	float EffectDurationSeconds = 3.0f;
	bool bImpactHandled = false;
};
