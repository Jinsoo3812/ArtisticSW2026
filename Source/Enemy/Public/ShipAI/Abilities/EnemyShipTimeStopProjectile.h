#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipTimeStopProjectile.generated.h"

class AEnemyShipTimeStopField;
class AEnemyShip;
class AShip;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
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
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ProjectileEffectComponent;

	/** Niagara effect that follows the time-stop projectile while it is in flight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects")
	TObjectPtr<UNiagaraSystem> ProjectileEffect = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectPlaybackSpeed = 1.0f;

	/** Niagara effect spawned when the projectile hits the Player Ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects")
	TObjectPtr<UNiagaraSystem> ExplosionEffect = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ExplosionEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ExplosionEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Effects", meta = (ClampMin = "0.01"))
	float ExplosionEffectPlaybackSpeed = 1.0f;

private:
	UFUNCTION()
	void OnProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnExplosionEffect(
		UNiagaraSystem* Effect,
		FVector_NetQuantize Location,
		FRotator Rotation,
		float UniformScale,
		float LifetimeScale,
		float PlaybackSpeed);

	UPROPERTY()
	TObjectPtr<AEnemyShip> SourceShip;

	UPROPERTY()
	TSubclassOf<AEnemyShipTimeStopField> FieldClass;

	float EffectRadius = 1500.0f;
	float EffectDurationSeconds = 3.0f;
	bool bImpactHandled = false;
};
