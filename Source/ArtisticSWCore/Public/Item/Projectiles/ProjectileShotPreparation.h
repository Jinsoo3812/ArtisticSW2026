#pragma once

#include "Item/Projectiles/ProjectileLaunchTypes.h"

class AArrowProjectile;
class UWorld;

namespace ProjectileShotPreparation
{
	ARTISTICSWCORE_API double GetServerTime(const UWorld* World);
	/** Uses the committed snapshot for logging/visualization, without resampling platform motion. */
	ARTISTICSWCORE_API void DebugShot(const AArrowProjectile& Arrow, const FProjectileShotSnapshot& Shot);
}
