#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Room/SWRoomStateAdapter.h"
#include "GravityVortexProjectile.generated.h"

USTRUCT()
struct FSWRoomGravityProjectileState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FVector PreviousLocation = FVector::ZeroVector;
	UPROPERTY(SaveGame) bool bActivated = false;
	UPROPERTY(SaveGame) bool bIncludeWaveHeight = true;
	UPROPERTY(SaveGame) float RemainingLife = 0.f;
	UPROPERTY(SaveGame) float GravityScale = 1.f;
	UPROPERTY(SaveGame) FSoftClassPath FieldClass;
};

class AGravityVortexField;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UNiagaraComponent;
class UNiagaraSystem;

/** A non-blocking projectile that activates only when it crosses a queried water surface. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AGravityVortexProjectile : public AActor, public ISWRoomStateAdapter
{
	GENERATED_BODY()

public:
	AGravityVortexProjectile();
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override
	{ return FSWRoomStructCodec::Compare<FSWRoomGravityProjectileState>(Expected, Actual, TimeToleranceSeconds, OutFields); }
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;

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
	FSWRoomGravityProjectileState PendingRoomState;
	bool bHasPendingRoomState = false;
};
