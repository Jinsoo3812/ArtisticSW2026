#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.generated.h"

/**
 * One server-authored Kelvin-like wake packet emitted by a moving ship.
 *
 * The renderer and every CPU buoyancy query evaluate the same packet data.
 * The packet is deliberately independent from UObject state so it is safe to
 * copy into the async Network Physics input snapshot.
 */
USTRUCT(BlueprintType)
struct WATERANDSHIP_API FSWShipWakeEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	int32 EventId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	FVector2D Forward = FVector2D(1.0, 0.0);

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	double StartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float InitialAmplitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float WaveLength = 600.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float PhaseSpeed = 650.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float Lifetime = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float KelvinHalfAngleRadians = 0.3398369f;

	double GetExpireServerTime() const { return StartServerTime + FMath::Max(Lifetime, 0.0f); }

	bool IsActiveAt(double ServerTime) const
	{
		return InitialAmplitude > 0.0f
			&& WaveLength > UE_SMALL_NUMBER
			&& ServerTime >= StartServerTime
			&& ServerTime < GetExpireServerTime();
	}
};

/** Shared deterministic approximation used by GT water queries and Async PT. */
struct WATERANDSHIP_API FSWShipWakeEvaluator
{
	static float EvaluateHeight(
		const FVector2D& QueryPosition,
		double ServerTime,
		TConstArrayView<FSWShipWakeEvent> Events);

	static FVector2D EvaluateGradient(
		const FVector2D& QueryPosition,
		double ServerTime,
		TConstArrayView<FSWShipWakeEvent> Events,
		float SampleDistance = 25.0f);
};

