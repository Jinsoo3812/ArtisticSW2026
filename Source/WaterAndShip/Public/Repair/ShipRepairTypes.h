#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ShipRepairTypes.generated.h"

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipRepairMaterialRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair", meta = (ClampMin = "0.0"))
	float HealthRestored = 20.0f;
};

struct WATERANDSHIP_API FShipRepairSpawnRules
{
	static int32 GetRequiredLeakCount(float HealthRatio, const TArray<float>& Thresholds, int32 MaxLeaks)
	{
		int32 Required = 0;
		for (const float Threshold : Thresholds)
		{
			Required += HealthRatio <= FMath::Clamp(Threshold, 0.0f, 1.0f) ? 1 : 0;
		}
		return FMath::Min(Required, FMath::Max(0, MaxLeaks));
	}

	static bool ShouldCreateLeak(
		int32 ActiveLeaks,
		int32 AvailableInactivePoints,
		int32 RequiredLeaks,
		float RandomChance,
		float RandomRoll)
	{
		return AvailableInactivePoints > 0
			&& (ActiveLeaks < RequiredLeaks
				|| RandomRoll < FMath::Clamp(RandomChance, 0.0f, 1.0f));
	}
};
