#include "SWRippleWaterWaves.h"
#include "RippleSubsystem.h"
#include "Engine/World.h"

USWRippleWaterWaves::USWRippleWaterWaves()
{
}

float USWRippleWaterWaves::GetMaxWaveHeight() const
{
	float MaxHeight = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			MaxHeight = ActualWaves->GetMaxWaveHeight();
		}
	}
	
	// Add an arbitrary maximum ripple allowance (e.g. 50cm) to let the physics engine
	// know that waves might peak slightly higher than the base Gerstner waves.
	return MaxHeight + 50.0f;
}

float USWRippleWaterWaves::GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const
{
	float Height = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			Height = ActualWaves->GetWaveHeightAtPosition(InPosition, InWaterDepth, InTime, OutNormal);
		}
		else
		{
			OutNormal = FVector::UpVector;
		}
	}
	else
	{
		OutNormal = FVector::UpVector;
	}

	if (UWorld* World = GetWorld())
	{
		if (URippleSubsystem* Subsystem = World->GetSubsystem<URippleSubsystem>())
		{
			Height += Subsystem->GetRippleHeight(InPosition);
		}
	}

	return Height;
}

float USWRippleWaterWaves::GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const
{
	float Height = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			Height = ActualWaves->GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, InTime);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (URippleSubsystem* Subsystem = World->GetSubsystem<URippleSubsystem>())
		{
			Height += Subsystem->GetRippleHeight(InPosition);
		}
	}

	return Height;
}

float USWRippleWaterWaves::GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth, float InMinDepth) const
{
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			return ActualWaves->GetWaveAttenuationFactor(InPosition, InWaterDepth, InMinDepth);
		}
	}
	return 1.0f;
}
