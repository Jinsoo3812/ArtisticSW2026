// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Room/SWRoomStateAdapter.h"
#include "Cannonball.generated.h"

USTRUCT()
struct FSWRoomCannonballState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid LaunchingShipId;
	UPROPERTY(SaveGame) bool bEnemyTeam = false;
	UPROPERTY(SaveGame) float DamageAmount = 0.f;
	UPROPERTY(SaveGame) FSoftClassPath DamageEffectClass;
	UPROPERTY(SaveGame) bool bHasHitWater = false;
	UPROPERTY(SaveGame) bool bHasProcessedShipHit = false;
	UPROPERTY(SaveGame) bool bHasProcessedBlockingImpact = false;
	UPROPERTY(SaveGame) bool bHasDesignatedImpact = false;
	UPROPERTY(SaveGame) FVector DesignatedImpactLocation = FVector::ZeroVector;
	UPROPERTY(SaveGame) float DesignatedImpactTolerance = 75.f;
	UPROPERTY(SaveGame) float GravityScale = 1.f;
	UPROPERTY(SaveGame) bool bWaterTimerPending = false;
	UPROPERTY(SaveGame) float WaterTimerRemaining = 0.f;
	UPROPERTY(SaveGame) bool bIsWaterBomb = false;
	UPROPERTY(SaveGame) float WaterBombDuration = 0.f;
	UPROPERTY(SaveGame) float WaterBombAttackSpeedMultiplier = 1.f;
};

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class AShip;
class UGameplayEffect;
class UNiagaraSystem;

UCLASS()
class WATERANDSHIP_API ACannonball : public AActor, public ISWRoomStateAdapter
{
	GENERATED_BODY()
	friend class URippleSubsystem;
	
public:	
	ACannonball();
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override
	{ return FSWRoomStructCodec::Compare<FSWRoomCannonballState>(Expected, Actual, TimeToleranceSeconds, OutFields); }
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;

public:	
	virtual void Tick(float DeltaTime) override;

	/** Initialize Projectile values on spawn */
	void InitializeProjectile(
		AShip* InLaunchingShip,
		float InDamage,
		float InSpeed,
		const FVector& InInheritedVelocity = FVector::ZeroVector);

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

	/** Runtime Niagara component attached to the interpolated projectile mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ProjectileEffectComponent;

	// ---- Properties ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	TSubclassOf<UGameplayEffect> DamageGEClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	float DamageAmount = 10.0f;

	/** Multiplies the cannon's resolved damage when the splash damages an enemy character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage", meta = (ClampMin = "0.0"))
	float EnemyDamageMultiplier = 1.0f;

	/** Multiplies the cannon's resolved damage when the splash damages a player character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage", meta = (ClampMin = "0.0"))
	float PlayerDamageMultiplier = 1.0f;

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

	/** Niagara effect that follows the cannonball while it is in flight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects")
	TObjectPtr<UNiagaraSystem> ProjectileEffect = nullptr;

	/** Size multiplier applied through the shared Niagara tuning system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ProjectileEffectPlaybackSpeed = 1.0f;

	/** Size multiplier applied through the shared Niagara tuning system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ShipImpactEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ShipImpactEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float ShipImpactEffectPlaybackSpeed = 1.0f;

	/** Additional Niagara effect spawned where this projectile enters water. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects")
	TObjectPtr<UNiagaraSystem> WaterImpactEffect = nullptr;

	/** Size multiplier applied through the shared Niagara tuning system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float WaterImpactEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float WaterImpactEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannonball|Effects", meta = (ClampMin = "0.01"))
	float WaterImpactEffectPlaybackSpeed = 1.0f;

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

	/** Preserves swept impact data for projectile subclasses that need the exact contact point. */
	virtual void HandleShipImpact(AShip* HitShip, const FHitResult& Hit);
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
	virtual UNiagaraSystem* GetProjectileEffect() const;
	virtual float GetProjectileEffectScale() const;
	virtual float GetProjectileEffectLifetimeScale() const;
	virtual float GetProjectileEffectPlaybackSpeed() const;
	virtual UNiagaraSystem* GetWaterImpactEffect() const;
	virtual float GetWaterImpactEffectScale() const;
	virtual float GetWaterImpactEffectLifetimeScale() const;
	virtual float GetWaterImpactEffectPlaybackSpeed() const;
	void SpawnNiagaraEffectForAll(UNiagaraSystem* Effect, const FVector& Location,
		float SizeScale = 1.0f, float LifetimeScale = 1.0f, float PlaybackSpeed = 1.0f);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnNiagaraEffect(UNiagaraSystem* Effect, FVector_NetQuantize Location,
		FRotator Rotation, float SizeScale, float LifetimeScale, float PlaybackSpeed,
		bool bIsWaterImpact);

private:
	UFUNCTION()
	void OnRep_LaunchingShip();
	void ConfigureProjectileCollision();

	// ---- State ----
	UPROPERTY(ReplicatedUsing = OnRep_LaunchingShip)
	TObjectPtr<AShip> LaunchingShip = nullptr;

	bool bHasHitWater = false;
	bool bHasProcessedShipHit = false;
	bool bHasProcessedBlockingImpact = false;
	bool bHasDesignatedImpact = false;
	FVector DesignatedImpactLocation = FVector::ZeroVector;
	FVector PreviousProjectileLocation = FVector::ZeroVector;
	float DesignatedImpactTolerance = 75.0f;
	FTimerHandle WaterHitTimerHandle;
	FSWRoomCannonballState PendingRoomState;
	bool bHasPendingRoomState = false;
	bool bHasSavedTeam = false;
	bool bSavedEnemyTeam = false;
	FVector PreviousWaterProbeLocation = FVector::ZeroVector;
	float PreviousWaterProbeSurfaceZ = 0.0f;
	bool bHasPreviousWaterProbe = false;
};
