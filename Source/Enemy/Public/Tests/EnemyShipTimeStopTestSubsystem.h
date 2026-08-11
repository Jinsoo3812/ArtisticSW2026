#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EnemyShipTimeStopTestSubsystem.generated.h"

class AEnemyShipTimeStopField;
class AShip;

/** Command-line-only Test_Level network probe for external Time Stop world locking. */
UCLASS()
class ENEMY_API UEnemyShipTimeStopTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void TryStartServerProbe();
	void TickServerProbe(double Now);
	void TickClientProbe(double Now);
	void Finish(bool bPassed, const TCHAR* Reason);
	AShip* FindPlayerShip() const;

	TWeakObjectPtr<AShip> TargetShip;
	TWeakObjectPtr<AEnemyShipTimeStopField> ProbeField;
	double WorldBeginTime = 0.0;
	double ProbeStartTime = 0.0;
	FVector AnchorLocation = FVector::ZeroVector;
	float MaximumObservedDrift = 0.0f;
	bool bAppliedImpulse = false;
	bool bObservedActiveLock = false;
	bool bFinished = false;
};
