#pragma once

#include "Item/Projectiles/ProjectileLaunchTypes.h"

class AActor;

/** Player bow: socket-to-aim relative velocity plus current ship point velocity, committed once. */
namespace PlayerBowShotPreparation
{
	CLASSFEATURE_API double GetGravityScale(double AuthoredScale);
	/** Ground/air shots inherit zero; a supported ship must provide a valid point velocity. */
	CLASSFEATURE_API bool Prepare(const AActor* Shooter, const FProjectileShotInput& Input,
		FProjectileShotSnapshot& OutShot);
}
