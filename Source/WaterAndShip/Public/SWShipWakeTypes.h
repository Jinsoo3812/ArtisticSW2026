#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.generated.h"

/**
 * One server-authored M4 Kelvin atlas generator. The same state and baked FP16
 * atlas are consumed by Water WPO, game-thread water queries and Async Physics.
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

	/** Newest-to-oldest vessel path. Point zero is the current Kelvin apex. */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M4")
	TArray<FVector2D> TrajectoryPoints;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	double UpdateServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float Amplitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float SpeedCmPerSecond = 0.0f;

	/** Raw hull translation speed used only to extrapolate source position between updates. */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float AdvectionSpeedCmPerSecond = 0.0f;

	/** Gaussian pressure length b used by Fr = V / sqrt(g b). */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M4")
	float PressureSizeCm = 2400.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M4")
	float LongitudinalScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M4")
	float LateralScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M4")
	float NearHullSuppressDistanceCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake")
	float HullLengthCm = 2400.0f;

	/** Distance from the bow pressure source to the stern pressure source. */
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake|M6")
	float SternOffsetCm = 900.0f;

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
			&& PressureSizeCm > UE_SMALL_NUMBER
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

