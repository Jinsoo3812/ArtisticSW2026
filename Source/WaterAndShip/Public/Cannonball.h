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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float LifeTimeAfterWaterHit = 2.0f;

	/** Initial amplitude for water ripple */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float RippleAmplitude = 50.0f;

protected:
	// Water remains overlap-driven so the authoritative WaterBody delegate can
	// create and replicate the ripple. Ship damage is handled by swept blocking hits.
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	virtual void HandleShipHit(AShip* HitShip);
	virtual void HandleWaterOverlap(
		AActor* WaterActor,
		UPrimitiveComponent* WaterComponent,
		bool bFromSweep,
		const FHitResult& SweepResult);
	AShip* GetLaunchingShip() const { return LaunchingShip; }
	virtual void TriggerWaterRipple(const FVector& HitLocation);
	void MarkWaterHitHandledWithoutDeactivation();
	void DeactivateProjectile();

private:
	// ---- State ----
	UPROPERTY()
	TObjectPtr<AShip> LaunchingShip = nullptr;

	bool bHasHitWater = false;
	bool bHasProcessedShipHit = false;
	bool bHasDesignatedImpact = false;
	FVector DesignatedImpactLocation = FVector::ZeroVector;
	FVector PreviousProjectileLocation = FVector::ZeroVector;
	float DesignatedImpactTolerance = 75.0f;
	FTimerHandle WaterHitTimerHandle;
};
