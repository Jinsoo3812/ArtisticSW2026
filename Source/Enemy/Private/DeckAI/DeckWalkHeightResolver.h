#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckWalkTypes.h"

class AEnemyShip;

/** A server build snapshot. Authored settings and reference components are never modified. */
struct FDeckWalkHeightBand
{
	float MinimumZ = 0.0f;
	float MaximumZ = 0.0f;
	FVector ReferenceLocalPosition = FVector::ZeroVector;
	bool bUseReferenceHeight = false;
};

/** Resolves authoring into a shared ship-local height band, independent of collision/pathfinding. */
class FDeckWalkHeightResolver
{
public:
	static bool Resolve(const FDeckWalkSurfaceSettings& Surface, const AEnemyShip& Ship,
		const FTransform& Frame, FDeckWalkHeightBand& OutBand, FString& OutError);
};
