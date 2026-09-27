#pragma once

#include "CoreMinimal.h"

/** Pure, deterministic roll-controller math shared by normal PT simulation and resimulation. */
struct WATERANDSHIP_API FShipRollStabilizationMath
{
	static float ComputeSignedRollRadians(const FQuat& Rotation);

	static float ComputeAngularAccelerationRadians(
		float RollRadians,
		float RollAngularVelocityRadians,
		float SoftLimitDegrees,
		float MaximumAngleDegrees,
		float NaturalFrequencyHz,
		float DampingRatio,
		float MaximumAngularAccelerationDegrees);
};
