#pragma once

#include "CoreMinimal.h"
#include "Cannonball.h"
#include "EnemyShipTorpedo.generated.h"

class AShip;
class USWBuoyancyComponent;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;

/** Dedicated Enemy Ship projectile: direct Player Ship damage, no area damage. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTorpedo : public ACannonball
{
	GENERATED_BODY()

public:
	AEnemyShipTorpedo();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_ReplicatedMovement() override;

	void InitializeTorpedo(
		AShip* InLaunchingShip,
		AShip* InDesignatedTarget,
		float InSnapshotDamage,
		float InSpeed,
		float InMaximumFlightSeconds);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Torpedo")
	float GetSnapshotDamage() const { return DamageAmount; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Torpedo")
	AShip* GetDesignatedTarget() const { return DesignatedTarget.Get(); }

	bool HasEnteredWaterForDiagnostics() const { return bWaterEntryObserved; }
	bool IsBuoyancyEnabledForDiagnostics() const { return bBuoyancyEnabled; }
	float GetWaterEntryZForDiagnostics() const { return WaterEntryZ; }
	float GetMinimumPostEntryZForDiagnostics() const { return MinimumPostEntryZ; }
	float GetMaximumPostBuoyancyZForDiagnostics() const { return MaximumPostBuoyancyZ; }

protected:
	virtual void HandleShipHit(AShip* HitShip) override;
	virtual void HandleWaterOverlap(
		AActor* WaterActor,
		UPrimitiveComponent* WaterComponent,
		bool bFromSweep,
		const FHitResult& SweepResult) override;
	virtual void TriggerWaterRipple(const FVector& HitLocation) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy")
	TObjectPtr<USWBuoyancyComponent> SWBuoyancyComponent;

	/** Translucent emissive overlay; preserves the authored torpedo surface material underneath. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Visual")
	TObjectPtr<UMaterialInterface> PulseOverlayMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Fuse")
	TObjectPtr<UNiagaraComponent> FuseBurstComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Fuse")
	TObjectPtr<UNiagaraSystem> FuseBurstSystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Fuse")
	FName FuseSocketName = TEXT("FuseTip");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Fuse", meta = (ClampMin = "0.05", Units = "s"))
	float FuseBurstIntervalSeconds = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Fuse", meta = (ClampMin = "0.01"))
	float FuseBurstScale = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "1.0", Units = "cm"))
	float FloatingPontoonRadius = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "1.0", Units = "kg"))
	float FloatingMassKg = 25.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "0.0"))
	float FloatingLinearDamping = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "0.0"))
	float FloatingAngularDamping = 3.0f;

	/** Keeps gravity-only rigid-body motion briefly after water entry so the torpedo visibly splashes down before floating. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "0.0", Units = "s"))
	float BuoyancyActivationDelaySeconds = 0.5f;

	/** Client-only smoothing for the server-authoritative floating rigid body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Networking", meta = (ClampMin = "0.0"))
	float ClientLocationInterpSpeed = 14.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Networking", meta = (ClampMin = "0.0"))
	float ClientRotationInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Networking", meta = (ClampMin = "0.0", Units = "s"))
	float ClientMaxExtrapolationTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Networking", meta = (ClampMin = "0.0", Units = "cm"))
	float ClientNetworkSnapDistance = 500.0f;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastTorpedoExploded(const FVector& ExplosionLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy Ship|Torpedo", meta = (DisplayName = "On Torpedo Exploded"))
	void K2_OnTorpedoExploded(const FVector& ExplosionLocation);

private:
	UFUNCTION()
	void OnRep_IsFloating();

	void ApplyWaterEntryPhysicsState();
	void EnableBuoyancyAfterDelay();
	void DetectDamageMeshContactAfterWater();
	void RestartFuseBurst();

	TWeakObjectPtr<AShip> DesignatedTarget;

	UPROPERTY(ReplicatedUsing = OnRep_IsFloating)
	bool bIsFloating = false;

	bool bExplosionConsumed = false;
	bool bWaterEntryObserved = false;

	UPROPERTY(Replicated)
	bool bBuoyancyEnabled = false;
	float WaterEntryZ = 0.0f;
	float MinimumPostEntryZ = TNumericLimits<float>::Max();
	float MaximumPostBuoyancyZ = -TNumericLimits<float>::Max();
	bool bHasClientMovementTarget = false;
	FVector ClientMovementTargetLocation = FVector::ZeroVector;
	FQuat ClientMovementTargetRotation = FQuat::Identity;
	FVector ClientMovementTargetVelocity = FVector::ZeroVector;
	float ClientMovementTargetReceiveTime = 0.0f;
	FVector PreviousWaterPhysicsLocation = FVector::ZeroVector;
	FTimerHandle BuoyancyActivationTimerHandle;
	FTimerHandle FuseBurstTimerHandle;
};
