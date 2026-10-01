#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/** Player camera target selection only. Never spawns arrows or decides flight collisions. */
namespace PlayerBowAimResolver
{
	CLASSFEATURE_API bool Resolve(const UWorld* World, const AActor* Shooter, const AActor* Weapon,
		const FVector& Muzzle, const FVector& ViewOrigin, const FVector& ViewDirection,
		double TraceDistance, const FGuid& ShotId, FVector& OutAimPoint, FVector& OutViewDirection);
}
