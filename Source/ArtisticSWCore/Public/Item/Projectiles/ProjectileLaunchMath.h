#pragma once

#include "CoreMinimal.h"
#include "Item/Projectiles/ProjectileLaunchTypes.h"

namespace ProjectileLaunchMath
{
	/** Natural gravity acts after release. WorldAim never bends aim to follow the shooter. */
	ARTISTICSWCORE_API bool BuildVelocity(const FVector& AimDirection, double Speed,
		EProjectileVelocityPolicy Policy, const FVector& ShooterVelocity, FVector& OutWorldVelocity);
	/** Parallax correction with a forward fallback when a camera hit is behind the muzzle. */
	ARTISTICSWCORE_API bool ResolveMuzzleDirection(const FVector& Muzzle, const FVector& AimPoint,
		const FVector& ViewDirection, FVector& OutDirection);
}
