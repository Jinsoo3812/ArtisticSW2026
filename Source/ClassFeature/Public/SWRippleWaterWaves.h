#pragma once

#include "CoreMinimal.h"
#include "WaterWaves.h"
#include "SWRippleWaterWaves.generated.h"

UCLASS(BlueprintType, EditInlineNew)
class CLASSFEATURE_API USWRippleWaterWaves : public UWaterWavesBase
{
	GENERATED_BODY()

public:
	USWRippleWaterWaves();

	// UWaterWavesBase interface overrides
	virtual float GetMaxWaveHeight() const override;
	virtual float GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const override;
	virtual float GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const override;
	virtual float GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth, float InMinDepth) const override;

	// The base wave generator (e.g. Gerstner Water Waves) to delegate standard wave computation to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
	TObjectPtr<UWaterWavesBase> BaseWaves;
};
