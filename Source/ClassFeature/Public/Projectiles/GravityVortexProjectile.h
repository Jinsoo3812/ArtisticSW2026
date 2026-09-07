#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GravityVortexProjectile.generated.h"

class AGravityVortexField;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UNiagaraComponent;
class UNiagaraSystem;

/** A non-blocking projectile that activates only when it crosses a queried water surface. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AGravityVortexProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGravityVortexProjectile();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;

	void LaunchProjectile(const FVector& LaunchVelocity);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ProjectileEffectComponent;

	/** Optional explicit mesh assignment. Null preserves the mesh authored directly on VisualMesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation")
	TObjectPtr<UStaticMesh> ProjectileMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation", meta = (ClampMin = "0.001"))
	float ProjectileMeshScale = 1.0f;

	/** Continuous trail effect using the same tuning adapter as cannon projectiles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation")
	TObjectPtr<UNiagaraSystem> ProjectileEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation", meta = (ClampMin = "0.01"))
	float ProjectileEffectScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation", meta = (ClampMin = "0.01"))
	float ProjectileEffectLifetimeScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile|Presentation", meta = (ClampMin = "0.01"))
	float ProjectileEffectPlaybackSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Activation")
	TSubclassOf<AGravityVortexField> FieldClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile", meta = (ClampMin = "0.1", Units = "s"))
	float MaxProjectileLifetime = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Projectile")
	bool bIncludeWaveHeight = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug")
	bool bDrawDebug = true;

protected:
	virtual void BeginPlay() override;

private:
	bool QueryWaterSurfaceAtLocation(const FVector& Location, float& OutWaterSurfaceZ) const;
	void ActivateAtWaterSurface(const FVector& SurfaceLocation);

	FVector PreviousLocation = FVector::ZeroVector;
	bool bActivated = false;
};
