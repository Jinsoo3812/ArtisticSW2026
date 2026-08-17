#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.generated.h"

UENUM(BlueprintType)
enum class ESWKelvinFroudeProfile : uint8
{
	Fr_0_30 UMETA(DisplayName = "Fr 0.30 (Transverse Dominant)"),
	Fr_0_50 UMETA(DisplayName = "Fr 0.50 (Balanced Classical Kelvin)"),
	Fr_0_70 UMETA(DisplayName = "Fr 0.70 (Transition Wake)"),
	Fr_1_00 UMETA(DisplayName = "Fr 1.00 (Narrow Divergent Wake 14.3 deg)")
};

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
	UPROPERTY(BlueprintReadOnly, Category = "Ship Wake") ESWKelvinFroudeProfile FroudeProfile = ESWKelvinFroudeProfile::Fr_0_50;

	bool IsActiveAt(const double ServerTime) const
	{
		return InitialAmplitudeCm > 0.0f
			&& PropagationSpeedCmPerSecond > UE_SMALL_NUMBER
			&& ServerTime >= StartServerTime
			&& ServerTime < ExpireServerTime;
	}
};

struct FSWShipWakeDebugSample
{
	float WeightedHeight = 0.0f;
	float BlendWeight = 0.0f;
	float FinalHeight = 0.0f;
	int32 ActiveContributingEvents = 0;
	int32 TotalEventsChecked = 0;
	FString DetailLog;
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

	static FSWShipWakeDebugSample EvaluateDebug(
		const FVector2D& QueryPosition,
		double ServerTime,
		TConstArrayView<FSWShipWakeEvent> Events,
		bool bIncludeEventDetails = false);

	static FVector2D EvaluateGradient(
		const FVector2D& QueryPosition,
		double ServerTime,
		TConstArrayView<FSWShipWakeEvent> Events,
		float SampleDistance = 25.0f);
};
