#include "ShipRollStabilization.h"

float FShipRollStabilizationMath::ComputeSignedRollRadians(const FQuat& Rotation)
{
	const FVector Forward = Rotation.GetForwardVector().GetSafeNormal();
	const FVector ReferenceUp = FVector::VectorPlaneProject(FVector::UpVector, Forward).GetSafeNormal();
	const FVector CurrentUp = FVector::VectorPlaneProject(
		Rotation.GetUpVector(), Forward).GetSafeNormal();
	if (ReferenceUp.IsNearlyZero() || CurrentUp.IsNearlyZero())
	{
		return 0.0f;
	}
	return FMath::Atan2(
		FVector::DotProduct(FVector::CrossProduct(ReferenceUp, CurrentUp), Forward),
		FVector::DotProduct(ReferenceUp, CurrentUp));
}

float FShipRollStabilizationMath::ComputeAngularAccelerationRadians(
	float RollRadians,
	float RollAngularVelocityRadians,
	float SoftLimitDegrees,
	float MaximumAngleDegrees,
	float NaturalFrequencyHz,
	float DampingRatio,
	float MaximumAngularAccelerationDegrees)
{
	const float SoftLimit = FMath::DegreesToRadians(FMath::Max(0.0f, SoftLimitDegrees));
	const float MaximumAngle = FMath::DegreesToRadians(
		FMath::Max(SoftLimitDegrees + UE_KINDA_SMALL_NUMBER, MaximumAngleDegrees));
	const float NaturalFrequency = 2.0f * UE_PI * FMath::Max(0.0f, NaturalFrequencyHz);
	const float SafeDampingRatio = FMath::Max(0.0f, DampingRatio);
	const float MaximumAcceleration = FMath::DegreesToRadians(
		FMath::Max(0.0f, MaximumAngularAccelerationDegrees));

	// The linear term is a standard second-order controller. A damping ratio of one
	// is critically damped, so it returns upright without introducing oscillation.
	float Acceleration = -FMath::Square(NaturalFrequency) * RollRadians
		- 2.0f * SafeDampingRatio * NaturalFrequency * RollAngularVelocityRadians;

	// Smoothstep adds a monotonic soft-wall potential from SoftLimit to MaximumAngle.
	// It has zero slope at entry, avoiding a torque discontinuity during resimulation.
	const float NormalizedExcess = FMath::Clamp(
		(FMath::Abs(RollRadians) - SoftLimit) / FMath::Max(UE_KINDA_SMALL_NUMBER, MaximumAngle - SoftLimit),
		0.0f,
		1.0f);
	const float LimitBlend = NormalizedExcess * NormalizedExcess * (3.0f - 2.0f * NormalizedExcess);
	Acceleration -= FMath::Sign(RollRadians) * LimitBlend * MaximumAcceleration;
	return FMath::Clamp(Acceleration, -MaximumAcceleration, MaximumAcceleration);
}
