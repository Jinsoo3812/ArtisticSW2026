#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EnemyShipObstacleBuoyancyTestSubsystem.generated.h"

class AEnemyShipObstacle;

/** Command-line-only Test_Level regression probe for obstacle water entry and buoyancy. */
UCLASS()
class ENEMY_API UEnemyShipObstacleBuoyancyTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	bool FindWaterTestPoint(FVector& OutSurfaceLocation) const;
	void SpawnProbe();
	void FinishTest();
	void TickClientCollisionProbe(double Now);

	TWeakObjectPtr<AEnemyShipObstacle> ProbeObstacle;
	double WorldBeginTime = 0.0;
	double ProbeSpawnTime = 0.0;
	float WaterSurfaceZ = 0.0f;
	float MinimumObservedZ = TNumericLimits<float>::Max();
	float MaximumObservedZAfterBuoyancy = -TNumericLimits<float>::Max();
	FVector2D SpawnXY = FVector2D::ZeroVector;
	float MaximumObservedXYDrift = 0.0f;
	bool bObservedWaterEntry = false;
	bool bObservedBuoyancy = false;
	bool bAppliedHorizontalImpulse = false;
	bool bFinished = false;
};
