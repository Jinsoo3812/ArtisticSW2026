#pragma once

#include "CoreMinimal.h"

class AWaterBody;
class UWorld;

/** Lightweight shared Water Plugin queries used by ship skills and projectiles. */
struct WATERANDSHIP_API FWaterSurfaceQueryLibrary
{
	/**
	 * Finds the highest valid water surface at Location.XY.
	 * Wave evaluation is optional because placement previews normally want the stable base surface.
	 */
	static bool QueryWaterSurface(
		const UWorld* World,
		const FVector& Location,
		float& OutWaterSurfaceZ,
		bool bIncludeWaveHeight = false);

	/** Same query against one already-known Water Body. */
	static bool QueryWaterBodySurface(
		const AWaterBody* WaterBody,
		const FVector& Location,
		double QueryTimeSeconds,
		float& OutWaterSurfaceZ,
		bool bIncludeWaveHeight = false);
};
