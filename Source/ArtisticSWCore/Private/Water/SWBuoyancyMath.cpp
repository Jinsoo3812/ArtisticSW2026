#include "Water/SWBuoyancyMath.h"

FSWBuoyancySolveResult FSWBuoyancyMath::SolvePontoon(
	const FSWBuoyancySolveInput& Input,
	const FSWBuoyancyForceSettings& Settings)
{
	FSWBuoyancySolveResult Result;
	const float Radius = FMath::Max(Input.PontoonRadius, UE_SMALL_NUMBER);
	const float PontoonBottom = Input.PontoonCenterZ - Radius;
	Result.ImmersionDepth = Input.WaterHeight - PontoonBottom;

	if (Result.ImmersionDepth <= 0.0f)
	{
		return Result;
	}

	Result.bIsInWater = true;
	const float Submersion = FMath::Clamp(Result.ImmersionDepth, 0.0f, 2.0f * Radius);
	Result.SubmergedVolume = (PI / 3.0f) * FMath::Square(Submersion) * ((3.0f * Radius) - Submersion);
	const float DeepWaterAlpha = FMath::Clamp((Submersion - Radius) / Radius, 0.0f, 1.0f);
	const float EffectiveBuoyancyCoefficient = Settings.BuoyancyCoefficient * FMath::Lerp(
		1.0f,
		FMath::Max(Settings.DeepWaterBuoyancyMultiplier, 1.0f),
		DeepWaterAlpha);

	const float LinearDrag = Settings.BuoyancyDamp * Input.RelativeVelocityZ;
	const float QuadraticDrag = FMath::Sign(Input.RelativeVelocityZ)
		* Settings.BuoyancyDamp2
		* FMath::Square(Input.RelativeVelocityZ);
	Result.DampingForce = -FMath::Max(LinearDrag + QuadraticDrag, 0.0f);

	const float RawForce = (Result.SubmergedVolume * EffectiveBuoyancyCoefficient) + Result.DampingForce;
	Result.BuoyantForceZ = FMath::Clamp(
		RawForce * FMath::Max(Input.ForceScale, 0.0f),
		0.0f,
		Settings.MaxBuoyantForce);

	return Result;
}
