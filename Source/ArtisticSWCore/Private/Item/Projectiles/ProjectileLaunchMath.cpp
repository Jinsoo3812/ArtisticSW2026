#include "Item/Projectiles/ProjectileLaunchMath.h"

bool ProjectileLaunchMath::BuildVelocity(const FVector& AimDirection, double Speed,
	EProjectileVelocityPolicy Policy, const FVector& ShooterVelocity, FVector& OutWorldVelocity)
{
	OutWorldVelocity = FVector::ZeroVector;
	if (AimDirection.ContainsNaN() || AimDirection.IsNearlyZero()
		|| !FMath::IsFinite(Speed) || Speed <= UE_SMALL_NUMBER) return false;
	OutWorldVelocity = AimDirection.GetSafeNormal() * Speed;
	switch (Policy)
	{
	case EProjectileVelocityPolicy::WorldAim:
		break;
	case EProjectileVelocityPolicy::PhysicalInheritance:
		if (ShooterVelocity.ContainsNaN()) return false;
		OutWorldVelocity += ShooterVelocity;
		break;
	default:
		return false;
	}
	return !OutWorldVelocity.ContainsNaN() && !OutWorldVelocity.IsNearlyZero();
}

bool ProjectileLaunchMath::ResolveMuzzleDirection(const FVector& Muzzle, const FVector& AimPoint,
	const FVector& ViewDirection, FVector& OutDirection)
{
	OutDirection = FVector::ZeroVector;
	if (Muzzle.ContainsNaN() || AimPoint.ContainsNaN() || ViewDirection.ContainsNaN()
		|| ViewDirection.IsNearlyZero()) return false;
	const FVector Forward = ViewDirection.GetSafeNormal();
	const FVector ToTarget = AimPoint - Muzzle;
	OutDirection = FVector::DotProduct(ToTarget, Forward) > 10.0 ? ToTarget.GetSafeNormal() : Forward;
	return !OutDirection.IsNearlyZero();
}
