#pragma once

#include "CoreMinimal.h"
#include "SWBuoyancyTypes.generated.h"

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWBuoyancyForceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Force", meta = (ClampMin = "0.0"))
	float BuoyancyCoefficient = 0.1f;

	/**
	 * Extra buoyancy used only as the pontoon goes from half to fully submerged.
	 * It accelerates deep-water recovery without changing the near-surface equilibrium.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Force", meta = (ClampMin = "1.0"))
	float DeepWaterBuoyancyMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Force", meta = (ClampMin = "0.0"))
	float BuoyancyDamp = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Force", meta = (ClampMin = "0.0"))
	float BuoyancyDamp2 = 1.0f;

	/** Per-pontoon upward force clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Force", meta = (ClampMin = "0.0"))
	float MaxBuoyantForce = 5000000.0f;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWBuoyancyPontoon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Pontoons")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Pontoons")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Pontoons", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Pontoons", meta = (ClampMin = "0.0"))
	float ForceScale = 1.0f;
};

struct ARTISTICSWCORE_API FSWBuoyancySolveInput
{
	float WaterHeight = -BIG_NUMBER;
	float PontoonCenterZ = 0.0f;
	float PontoonRadius = 100.0f;
	float RelativeVelocityZ = 0.0f;
	float ForceScale = 1.0f;
};

struct ARTISTICSWCORE_API FSWBuoyancySolveResult
{
	float ImmersionDepth = 0.0f;
	float SubmergedVolume = 0.0f;
	float DampingForce = 0.0f;
	float BuoyantForceZ = 0.0f;
	bool bIsInWater = false;
};
