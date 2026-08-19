#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyShipTorpedoBuoyancyTestSubsystem.generated.h"

class AEnemyShip;
class AEnemyShipTorpedo;

/**
 * Opt-in runtime harness used by command-line standalone and client/server tests.
 * It is never created unless -EnemyShipTorpedoBuoyancyTest is present.
 */
UCLASS()
class ENEMY_API UEnemyShipTorpedoBuoyancyTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	bool FindWaterTestPoint(FVector& OutSurfaceLocation) const;
	void SpawnServerProbe();
	void FindReplicatedProbe();
	void FinishTest();

	TWeakObjectPtr<AEnemyShipTorpedo> ProbeTorpedo;
	TWeakObjectPtr<AEnemyShip> ProbeSourceShip;
	double WorldBeginTime = 0.0;
	double ProbeAcquiredTime = 0.0;
	FVector FirstObservedLocation = FVector::ZeroVector;
	FVector LastObservedLocation = FVector::ZeroVector;
	float ObservedMinimumZ = TNumericLimits<float>::Max();
	float ObservedMaximumZAfterBuoyancy = -TNumericLimits<float>::Max();
	bool bObservedFlightMovement = false;
	bool bObservedWaterEntry = false;
	bool bObservedBuoyancyEnabled = false;
	bool bTestFinished = false;
};
