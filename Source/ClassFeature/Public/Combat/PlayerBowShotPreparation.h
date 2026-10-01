#pragma once

#include "Item/Projectiles/ProjectileLaunchTypes.h"

class AActor;

/** Player bow initial conditions: direct socket-to-aim velocity, followed by natural gravity. */
namespace PlayerBowShotPreparation
{
	CLASSFEATURE_API double GetGravityScale(double AuthoredScale);
	CLASSFEATURE_API bool Prepare(const AActor* Shooter, const FProjectileShotInput& Input,
		FProjectileShotSnapshot& OutShot);
}
