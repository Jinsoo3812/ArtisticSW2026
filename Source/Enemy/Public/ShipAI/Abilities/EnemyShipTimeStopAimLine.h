#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipTimeStopAimLine.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class AShip;
class ACannon;

/** Warning laser that follows its cannon and continuously aims at the designated Player Ship. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTimeStopAimLine : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipTimeStopAimLine();

	void InitializeAimLine(
		const FVector& InStart,
		const FVector& InDirection,
		AShip* InTargetShip,
		float InMaximumDistance,
		float InTraceIntervalSeconds);
	void InitializeAimLineFromCannon(
		ACannon* InSourceCannon,
		AShip* InTargetShip,
		float InMaximumDistance,
		float InTraceIntervalSeconds);

	/** Stops target tracking at a world point while keeping the ray attached to the moving muzzle. */
	void LockAimTargetPoint(const FVector& InWorldTargetPoint);
	void BeginLockedCharge(UNiagaraSystem* InChargeEffect, float InSizeScale,
		float InLifetimeScale, float InPlaybackSpeed);
	void PlayInstantHitEffects(
		UNiagaraSystem* InTrailEffect,
		UNiagaraSystem* InExplosionEffect,
		const FVector& InStart,
		const FVector& InEnd,
		bool bHitPlayer,
		float InTrailScale,
		float InTrailLifetimeSeconds,
		float InTrailPlaybackSpeed,
		float InExplosionScale,
		float InExplosionLifetimeScale,
		float InExplosionPlaybackSpeed,
		float InPresentationLifetime);

	virtual void Tick(float DeltaSeconds) override;

	static FVector ResolveClippedLineEnd(
		const FVector& InStart,
		const FVector& InDirection,
		const AShip* InTargetShip,
		float InMaximumDistance);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop|Aim Line")
	FVector GetLineStart() const { return LineStart; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop|Aim Line")
	FVector GetLineEnd() const { return LineEnd; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ChargeEffectComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aim Line", meta = (ClampMin = "1.0", Units = "cm"))
	float LineThickness = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aim Line")
	TObjectPtr<UMaterialInterface> LaserMaterial;

	/** Client render smoothing for the replicated ray end; the muzzle start follows every local frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aim Line|Networking", meta = (ClampMin = "0.0"))
	float ClientEndpointInterpolationSpeed = 20.0f;

private:
	UFUNCTION()
	void OnRep_LineEndpoints();

	UFUNCTION()
	void OnRep_LineVisibility();

	UFUNCTION()
	void OnRep_SourceCannon();

	void UpdateClippedEndpoint();
	void RefreshLineVisual();
	void RefreshChargeEffect();

	UFUNCTION()
	void OnRep_ChargeState();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayInstantHitEffects(
		UNiagaraSystem* InTrailEffect,
		UNiagaraSystem* InExplosionEffect,
		FVector_NetQuantize InStart,
		FVector_NetQuantize InEnd,
		bool bHitPlayer,
		float InTrailScale,
		float InTrailLifetimeSeconds,
		float InTrailPlaybackSpeed,
		float InExplosionScale,
		float InExplosionLifetimeScale,
		float InExplosionPlaybackSpeed);

	UPROPERTY(ReplicatedUsing = OnRep_LineEndpoints)
	FVector_NetQuantize LineStart = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_LineEndpoints)
	FVector_NetQuantize LineEnd = FVector::ZeroVector;

	/** Lets each client bind presentation to its own smoothly rendered cannon muzzle. */
	UPROPERTY(ReplicatedUsing = OnRep_SourceCannon)
	TObjectPtr<ACannon> SourceCannon;

	UPROPERTY(ReplicatedUsing = OnRep_LineVisibility)
	bool bWarningLineVisible = true;

	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	TObjectPtr<UNiagaraSystem> ChargeEffect;

	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	float ChargeEffectScale = 1.0f;
	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	float ChargeEffectLifetimeScale = 1.0f;
	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	float ChargeEffectPlaybackSpeed = 1.0f;

	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	bool bChargeEffectActive = false;

	TWeakObjectPtr<AShip> TargetShip;
	FVector FixedDirection = FVector::ForwardVector;
	FVector PresentationLineStart = FVector::ZeroVector;
	FVector PresentationLineEnd = FVector::ZeroVector;
	bool bPresentationInitialized = false;
	FVector LockedTargetPoint = FVector::ZeroVector;
	bool bAimTargetLocked = false;
	float MaximumDistance = 200000.0f;
	float TraceIntervalSeconds = 0.05f;
	float TraceTimeAccumulator = 0.0f;
};
