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
