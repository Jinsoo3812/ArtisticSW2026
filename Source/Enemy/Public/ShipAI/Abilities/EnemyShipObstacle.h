#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipObstacle.generated.h"

class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;
class USWBuoyancyComponent;

/** Server-authoritative floating shield that blocks Player ships and cannonballs only. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipObstacle : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipObstacle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_ReplicatedMovement() override;

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Obstacle")
	bool HasEnteredWater() const { return bHasEnteredWater; }
	bool IsBuoyancyEnabledForDiagnostics() const { return bBuoyancyEnabled; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> ObstacleCollision;

	/** Kinematic wall matching the authored visual footprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ObstacleBlocker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ObstacleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USWBuoyancyComponent> SWBuoyancyComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Collision", meta = (ClampMin = "1.0", Units = "cm"))
	FVector CollisionHalfExtent = FVector(512.0f);

	/** Kept separate from the blocking shape so collision authoring cannot change flotation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Buoyancy", meta = (ClampMin = "1.0", Units = "cm"))
	float BuoyancyPontoonRadius = 50.0f;

	/** Torpedo-matched rigid-body mass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Buoyancy", meta = (ClampMin = "1.0", Units = "kg"))
	float FloatingMassKg = 25.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Buoyancy", meta = (ClampMin = "0.0"))
	float FloatingLinearDamping = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Buoyancy", meta = (ClampMin = "0.0"))
	float FloatingAngularDamping = 3.0f;

	/** Matches the torpedo's short plunge before buoyancy starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Buoyancy", meta = (ClampMin = "0.0", Units = "s"))
	float BuoyancyActivationDelaySeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle", meta = (ClampMin = "0.0", Units = "s"))
	float MaximumLifetimeSeconds = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Networking", meta = (ClampMin = "0.0"))
	float ClientLocationInterpSpeed = 14.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Networking", meta = (ClampMin = "0.0"))
	float ClientRotationInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Networking", meta = (ClampMin = "0.0", Units = "s"))
	float ClientMaxExtrapolationTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Obstacle|Networking", meta = (ClampMin = "0.0", Units = "cm"))
	float ClientNetworkSnapDistance = 500.0f;

private:
	UFUNCTION()
	void OnObstacleOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_HasEnteredWater();

	void ApplyPhysicsState();
	void EnableBuoyancy();

	UPROPERTY(ReplicatedUsing = OnRep_HasEnteredWater)
	bool bHasEnteredWater = false;

	UPROPERTY(Replicated)
	bool bBuoyancyEnabled = false;

	bool bHasClientMovementTarget = false;
	FVector ClientMovementTargetLocation = FVector::ZeroVector;
	FQuat ClientMovementTargetRotation = FQuat::Identity;
	FVector ClientMovementTargetVelocity = FVector::ZeroVector;
	float ClientMovementTargetReceiveTime = 0.0f;
	FTimerHandle BuoyancyActivationTimerHandle;
};
