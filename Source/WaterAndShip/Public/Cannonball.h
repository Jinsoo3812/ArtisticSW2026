// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cannonball.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class AShip;
class UGameplayEffect;
class UNiagaraSystem;

UCLASS()
class WATERANDSHIP_API ACannonball : public AActor
{
	GENERATED_BODY()
	
public:	
	ACannonball();

protected:
	virtual void BeginPlay() override;
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;

public:	
	virtual void Tick(float DeltaTime) override;

	/** Initialize Projectile values on spawn */
	void InitializeProjectile(AShip* InLaunchingShip, float InDamage, float InSpeed);

	/** Optional exact endpoint used by skills so terrain impacts do not continue below the Landscape. */
	void SetDesignatedImpactLocation(const FVector& InImpactLocation, float InArrivalTolerance = 75.0f);

protected:
	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> SphereCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CannonballMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// ---- Properties ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	TSubclassOf<UGameplayEffect> DamageGEClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	float DamageAmount = 10.0f;

	/** Full-damage splash radius evaluated when the projectile directly hits a ship. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage", meta = (ClampMin = "0.0", Units = "cm"))
	float SplashDamageRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float LifeTimeAfterWaterHit = 2.0f;

	/** Initial amplitude for water ripple */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float RippleAmplitude = 50.0f;

	/** Niagara effect used whenever a normal cannonball impacts an opposing ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects")
	TObjectPtr<UNiagaraSystem> ShipImpactEffect = nullptr;

	/** Uniform world scale applied to ShipImpactEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ShipImpactEffectScale = 1.0f;

	/** Additional Niagara effect spawned where this projectile enters water. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects")
	TObjectPtr<UNiagaraSystem> WaterImpactEffect = nullptr;

	/** Uniform world scale applied to WaterImpactEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float WaterImpactEffectScale = 1.0f;

protected:
	// Water remains overlap-driven so the authoritative WaterBody delegate can
	// create and replicate the ripple. Ship damage is handled by swept blocking hits.
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void OnProjectileStop(const FHitResult& ImpactResult);

	void HandleBlockingImpact(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit);

	virtual void HandleShipHit(AShip* HitShip);
	bool IsOpposingSplashTarget(const AActor* Candidate) const;
	bool ApplyDamageToTarget(AActor* TargetActor);
	virtual void HandleWaterOverlap(
		AActor* WaterActor,
		UPrimitiveComponent* WaterComponent,
		bool bFromSweep,
		const FHitResult& SweepResult);
	AShip* GetLaunchingShip() const { return LaunchingShip; }
	virtual void TriggerWaterRipple(const FVector& HitLocation);
	void MarkWaterHitHandledWithoutDeactivation();
	void DeactivateProjectile();
	void SpawnNiagaraEffectForAll(UNiagaraSystem* Effect, const FVector& Location, float UniformScale = 1.0f);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnNiagaraEffect(UNiagaraSystem* Effect, FVector_NetQuantize Location, FRotator Rotation, float UniformScale);

private:
	// ---- State ----
	UPROPERTY()
	TObjectPtr<AShip> LaunchingShip = nullptr;

	bool bHasHitWater = false;
	bool bHasProcessedShipHit = false;
	bool bHasProcessedBlockingImpact = false;
	bool bHasDesignatedImpact = false;
	FVector DesignatedImpactLocation = FVector::ZeroVector;
	FVector PreviousProjectileLocation = FVector::ZeroVector;
	float DesignatedImpactTolerance = 75.0f;
	FTimerHandle WaterHitTimerHandle;
};
