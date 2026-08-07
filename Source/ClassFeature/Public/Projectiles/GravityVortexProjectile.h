#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GravityVortexProjectile.generated.h"

class AGravityVortexField;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** A non-blocking projectile that activates only when it crosses a queried water surface. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AGravityVortexProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGravityVortexProjectile();

	virtual void Tick(float DeltaSeconds) override;

	void LaunchProjectile(const FVector& LaunchVelocity);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

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
