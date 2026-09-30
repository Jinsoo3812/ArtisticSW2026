#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AArrowProjectile;
class UWorld;
struct FProjectileShotSnapshot;

/** Shared obstacle policy for AI admission and actual arrow movement. Never ignores a carrier ship. */
namespace ArrowCollisionQuery
{
	/** Camera intent uses a ray; muzzle/flight clearance uses the authored small box. */
	ARTISTICSWCORE_API bool TraceObstacles(const UWorld* World, const FVector& Start, const FVector& End,
		const FCollisionQueryParams& Params, FHitResult& OutHit);
	/** Straight launch clearance/feedback, not a prediction of a future ballistic hit. */
	ARTISTICSWCORE_API bool IsAimObstructed(const UWorld* World, const FProjectileShotSnapshot& Shot,
		const FVector& HalfExtent, const FCollisionQueryParams& Params, FHitResult& OutHit);
	ARTISTICSWCORE_API bool SweepObstacles(const UWorld* World, const FVector& Start, const FVector& End,
		const FQuat& Rotation, const FVector& HalfExtent, const FCollisionQueryParams& Params, FHitResult& OutHit);
	/** Resolves the earliest obstacle or eligible character contact for one simulation substep. */
	ARTISTICSWCORE_API bool SweepFlight(const AArrowProjectile& Arrow, const FVector& Start, const FVector& End,
		const FQuat& Rotation, FHitResult& OutHit);
}
