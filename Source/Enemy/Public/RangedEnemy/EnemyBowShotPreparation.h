#pragma once

#include "Item/Projectiles/ProjectileLaunchTypes.h"

class AActor;

/** Enemy initial conditions. Same-ship aim compensates carrier motion and gravity, never target walking. */
namespace EnemyBowShotPreparation
{
	ENEMY_API double GetGravityScale(double AuthoredScale);
	ENEMY_API bool Prepare(const AActor* Shooter, const AActor* Target, const FProjectileShotInput& Input,
		FProjectileShotSnapshot& OutShot);
}
