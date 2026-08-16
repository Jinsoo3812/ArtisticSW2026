#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.generated.h"

/**
 * One server-authored steady Kelvin wake generator.
 *
 * M2 stores the current hull state rather than an expanding circular packet.
 * M3 uses it as a moving GPU-field source while CPU buoyancy queries retain the
 * M2 directional-spectrum approximation. The plain-data struct is safe to copy
 * into the async Network Physics input snapshot.
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
	double UpdateServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float Amplitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float SpeedCmPerSecond = 0.0f;

	/** Raw hull translation speed used only to extrapolate source position between updates. */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float AdvectionSpeedCmPerSecond = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float HullLengthCm = 2400.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float BeamWidthCm = 600.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float DraftCm = 250.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float WakeLengthCm = 12000.0f;

	/** How long the last replicated hull state remains valid without an update. */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float StateLifetime = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float TransverseStrength = 0.55f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float DivergentStrength = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float SternStrength = 0.72f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float SternPhaseOffsetRadians = 2.15f;

	double GetExpireServerTime() const { return UpdateServerTime + FMath::Max(StateLifetime, 0.0f); }

	bool IsActiveAt(double ServerTime) const
	{
		return Amplitude > 0.0f
			&& SpeedCmPerSecond > UE_SMALL_NUMBER
			&& HullLengthCm > UE_SMALL_NUMBER
			&& ServerTime >= UpdateServerTime
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

