#include "ShipAI/Abilities/EnemyShipSkillMath.h"

float FEnemyShipSkillMath::CalculateApproachSpeed(
	const FVector& SourceLocation,
	const FVector& SourceVelocity,
	const FVector& TargetLocation,
	const FVector& TargetVelocity)
{
	FVector ToTarget = TargetLocation - SourceLocation;
	ToTarget.Z = 0.0f;
	if (!ToTarget.Normalize())
	{
		return 0.0f;
	}

	FVector RelativeVelocity = SourceVelocity - TargetVelocity;
	RelativeVelocity.Z = 0.0f;
	return FMath::Max(0.0f, FVector::DotProduct(RelativeVelocity, ToTarget));
}

float FEnemyShipSkillMath::CalculateChargeDamage(
	float ApproachSpeed,
	float MinimumDamageSpeed,
	float DamagePerSpeedUnit,
	float MaximumDamage)
{
	const float Damage = FMath::Max(0.0f, ApproachSpeed - FMath::Max(0.0f, MinimumDamageSpeed))
		* FMath::Max(0.0f, DamagePerSpeedUnit);
	return MaximumDamage > 0.0f ? FMath::Min(Damage, MaximumDamage) : Damage;
}

bool FEnemyShipSkillMath::SuggestBallisticVelocity(
	const FVector& StartLocation,
	const FVector& TargetLocation,
	float LaunchSpeed,
	float GravityMagnitude,
	float PreferredAngleDegrees,
	FVector& OutLaunchVelocity,
	float& OutFlightTime,
	float& OutSolvedAngleDegrees)
{
	OutLaunchVelocity = FVector::ZeroVector;
	OutFlightTime = 0.0f;
	OutSolvedAngleDegrees = 0.0f;

	const float Speed = FMath::Max(0.0f, LaunchSpeed);
	const float Gravity = FMath::Max(0.0f, GravityMagnitude);
	const FVector Delta = TargetLocation - StartLocation;
	FVector HorizontalDelta(Delta.X, Delta.Y, 0.0f);
	const float HorizontalDistance = HorizontalDelta.Size();
	if (Speed <= KINDA_SMALL_NUMBER || Gravity <= KINDA_SMALL_NUMBER
		|| HorizontalDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const double SpeedSquared = static_cast<double>(Speed) * Speed;
	const double Horizontal = HorizontalDistance;
	const double Vertical = Delta.Z;
	const double Discriminant = SpeedSquared * SpeedSquared
		- static_cast<double>(Gravity)
			* (static_cast<double>(Gravity) * Horizontal * Horizontal + 2.0 * Vertical * SpeedSquared);
	if (Discriminant < 0.0)
	{
		return false;
	}

	const double Root = FMath::Sqrt(Discriminant);
	const double Denominator = static_cast<double>(Gravity) * Horizontal;
	const double TanLow = (SpeedSquared - Root) / Denominator;
	const double TanHigh = (SpeedSquared + Root) / Denominator;
	const double LowAngle = FMath::Atan(TanLow);
	const double HighAngle = FMath::Atan(TanHigh);
	const double PreferredRadians = FMath::DegreesToRadians(
		FMath::Clamp(PreferredAngleDegrees, -89.0f, 89.0f));
	const double SolvedAngle = FMath::Abs(HighAngle - PreferredRadians)
		< FMath::Abs(LowAngle - PreferredRadians)
		? HighAngle
		: LowAngle;

	const double HorizontalSpeed = static_cast<double>(Speed) * FMath::Cos(SolvedAngle);
	if (HorizontalSpeed <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	HorizontalDelta /= HorizontalDistance;
	OutLaunchVelocity = HorizontalDelta * static_cast<float>(HorizontalSpeed)
		+ FVector::UpVector * static_cast<float>(static_cast<double>(Speed) * FMath::Sin(SolvedAngle));
	OutFlightTime = static_cast<float>(Horizontal / HorizontalSpeed);
	OutSolvedAngleDegrees = static_cast<float>(FMath::RadiansToDegrees(SolvedAngle));
	return !OutLaunchVelocity.ContainsNaN() && FMath::IsFinite(OutFlightTime) && OutFlightTime > 0.0f;
}
