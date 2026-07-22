#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GravityVortexField.generated.h"

class AShip;
class UCurveFloat;

/** Server-authoritative radial field. Clients only use the replicated actor for visuals/debug. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AGravityVortexField : public AActor
{
	GENERATED_BODY()

public:
	AGravityVortexField();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "1.0", Units = "cm"))
	float PullRadius = 5000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float PullAcceleration = 900.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float MaxPullAcceleration = 1800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0", Units = "cm"))
	float InnerDeadZoneRadius = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgePullStrengthRatio = 0.25f;

	/** Optional curve: input 0=center, 1=edge; output is a strength multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull")
	TObjectPtr<UCurveFloat> PullFalloffCurve;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0"))
	float RadialDamping = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Pull", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxInwardSpeed = 2200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Lifetime", meta = (ClampMin = "0.05", Units = "s"))
	float Duration = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Lifetime", meta = (ClampMin = "0.01", Units = "s"))
	float TargetRefreshInterval = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|AI")
	bool bSuppressEnemyPropulsion = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug")
	bool bDrawDebug = true;

	/** Raises the range visualization above the water to avoid depth fighting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug", meta = (Units = "cm"))
	float DebugDrawHeightOffset = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug", meta = (ClampMin = "0.5"))
	float DebugLineThickness = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug", meta = (ClampMin = "16", ClampMax = "256"))
	int32 DebugCircleSegments = 96;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gravity Vortex|Network")
	double ActivationServerTime = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gravity Vortex|Network")
	double ExpireServerTime = 0.0;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RefreshTargets();
	void ReleaseShip(AShip* Ship);
	FVector CalculateAcceleration(AShip* Ship) const;
	double GetSynchronizedServerTime() const;

	FGuid SourceId;
	TSet<TWeakObjectPtr<AShip>> AffectedShips;
	float RefreshAccumulator = 0.0f;
};
