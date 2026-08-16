#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.generated.h"

/** M7 immutable source event; Forward is the trajectory tangent at emission. */
USTRUCT(BlueprintType)
struct WATERANDSHIP_API FSWShipWakeEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") int32 EventId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") FVector2D Origin = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") FVector2D Forward = FVector2D(1.0, 0.0);
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") double ExpireServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float InitialAmplitudeCm = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float PropagationSpeedCmPerSecond = 1200.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float DecayRate = 0.12f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float WakeLengthCm = 16000.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float WakeHalfWidthCm = 6000.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float EnvelopeWidthCm = 2500.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") float FadeInSeconds = 0.08f;

	bool IsActiveAt(const double ServerTime) const
	{
		return InitialAmplitudeCm > 0.0f
			&& PropagationSpeedCmPerSecond > UE_SMALL_NUMBER
			&& ServerTime >= StartServerTime
			&& ServerTime < ExpireServerTime;
	}
};

/** Identical deterministic function used by GT water queries and Async Physics. */
struct WATERANDSHIP_API FSWShipWakeEvaluator
{
	static float EvaluateEventHeight(
		const FVector2D& QueryPosition,
		double ServerTime,
		const FSWShipWakeEvent& Event);

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
