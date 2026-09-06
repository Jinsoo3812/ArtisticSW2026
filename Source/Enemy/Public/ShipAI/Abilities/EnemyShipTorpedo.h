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
UCLASS(Blueprintable, HideCategories=("Cannonball|Effects"))
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
	virtual void HandleShipImpact(AShip* HitShip, const FHitResult& Hit) override;
	virtual void HandleShipHit(AShip* HitShip) override;
	virtual UNiagaraSystem* GetProjectileEffect() const override;
	virtual float GetProjectileEffectScale() const override;
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

	/** Niagara effect that follows this torpedo while it is in flight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Effects")
	TObjectPtr<UNiagaraSystem> TorpedoProjectileEffect;

	/** Uniform component scale applied to TorpedoProjectileEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Effects", meta = (ClampMin = "0.01"))
	float TorpedoProjectileEffectScale = 1.0f;

	/** Niagara spawned when this torpedo explodes on the Player Ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Effects")
	TObjectPtr<UNiagaraSystem> ExplosionEffect;

	/** Uniform world scale applied to ExplosionEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Effects", meta = (ClampMin = "0.01"))
	float ExplosionEffectScale = 1.0f;

	/** Mass-independent acceleration applied to the Player Ship by a direct torpedo blast. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Blast", meta = (ClampMin = "0.0", ClampMax = "20000.0", Units = "cm/s^2"))
	float BlastAcceleration = 1200.0f;

	/** Duration of the Network Physics blast pulse. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Blast", meta = (ClampMin = "0.01", ClampMax = "1.0", Units = "s"))
	float BlastDurationSeconds = 0.15f;

	/** Adds an upward component to the explosion-to-centre direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Blast", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BlastUpwardBias = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "1.0", Units = "cm"))
	float FloatingPontoonRadius = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Buoyancy", meta = (ClampMin = "1.0", Units = "kg"))
	float FloatingMassKg = 25.0f;

	/** Vertical-only linear damping applied after water entry. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Water Drag", meta = (ClampMin = "0.0"))
	float FloatingLinearDamping = 8.0f;

	/** Horizontal force coefficient for F = -C1 * V. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Water Drag", meta = (ClampMin = "0.0"))
	float WaterHorizontalLinearDrag = 1.5f;

	/** Horizontal force coefficient for F = -C2 * |V| * V. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Water Drag", meta = (ClampMin = "0.0"))
	float WaterHorizontalQuadraticDrag = 0.002f;

	/** Safety cap for total horizontal drag force. Zero disables this cap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Water Drag", meta = (ClampMin = "0.0"))
	float MaximumHorizontalDragForce = 250000.0f;

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
	void ApplyWaterDrag(float DeltaSeconds);
	void EnableBuoyancyAfterDelay();
	void DetectDamageMeshContactAfterWater();
	void RestartFuseBurst();
	void LogVisualDiagnostics(const TCHAR* Phase) const;
	void LogPostBeginPlayVisualDiagnostics();
	void ProcessShipHit(AShip* HitShip, const FVector& ImpactPoint);

	TWeakObjectPtr<AShip> DesignatedTarget;

	UPROPERTY(ReplicatedUsing = OnRep_IsFloating)
	bool bIsFloating = false;

	bool bExplosionConsumed = false;
	bool bWaterEntryObserved = false;
	bool bHasLoggedFirstFuseActivation = false;

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
