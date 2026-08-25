#pragma once

#include "CoreMinimal.h"
#include "SWRippleTypes.generated.h"

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWRippleEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	int32 EventId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	double StartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	float InitialAmplitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	float WaveSpeed = 300.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	float DecayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	float WaveLength = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Water Ripple")
	double ExpireServerTime = 0.0;

	bool IsActiveAt(double ServerTime) const
	{
		return InitialAmplitude > 0.0f
			&& ServerTime >= StartServerTime
			&& ServerTime < ExpireServerTime;
	}
};

struct ARTISTICSWCORE_API FSWRippleEvaluator
{
	static float EvaluateHeight(
		const FVector2D& QueryPosition,
		double ServerTime,
		TConstArrayView<FSWRippleEvent> Events);
};

/**
 * Keeps replicated visual events from waiting on a lagging GameState clock.
 * The anchor only moves a client clock forward to an event that has already arrived;
 * it never predicts an event that the server has not replicated yet.
 */
struct ARTISTICSWCORE_API FSWRippleClientRenderClock
{
	void ObserveReplicatedEvent(double EventStartServerTime, double EstimatedServerTime, double LocalWorldTime);
	double Resolve(double EstimatedServerTime, double LocalWorldTime) const;
	bool IsAnchored() const { return bHasAnchor; }

private:
	bool bHasAnchor = false;
	double AnchorServerTime = 0.0;
	double AnchorLocalWorldTime = 0.0;
};

/** Matches a local cosmetic prediction to the later authoritative ripple. */
struct ARTISTICSWCORE_API FSWRipplePredictionPolicy
{
	static int32 FindBestPredictedEventIndex(
		TConstArrayView<FSWRippleEvent> Events,
		const FSWRippleEvent& AuthoritativeEvent,
		float MaxDistance,
		double MaxTimeDelta);
};

/** Deterministic capped-queue policy shared by authority and replicated clients. */
struct ARTISTICSWCORE_API FSWRippleQueuePolicy
{
	static int32 FindOldestEventIndex(TConstArrayView<FSWRippleEvent> Events);
	static void AddOrUpdateCapped(
		TArray<FSWRippleEvent>& Events,
		const FSWRippleEvent& Event,
		int32 MaxEventCount);
};
