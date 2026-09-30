#pragma once

#include "Item/Projectiles/ProjectileLaunchTypes.h"

class AActor;
class UWorld;

namespace ProjectileShotPreparation
{
	ARTISTICSWCORE_API double GetServerTime(const UWorld* World);
	/** WorldAim remains valid without a carrier or an available velocity provider. */
	ARTISTICSWCORE_API bool Prepare(const AActor* Shooter, const FProjectileShotInput& Input,
		FProjectileShotSnapshot& OutShot);
	ARTISTICSWCORE_API void DebugShot(const AActor* Shooter, const FProjectileShotSnapshot& Shot);
}
