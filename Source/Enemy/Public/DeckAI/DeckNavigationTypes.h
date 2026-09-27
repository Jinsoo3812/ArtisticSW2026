#pragma once

#include "CoreMinimal.h"

/** Immutable, ship-local node used by deck route searches. */
struct ENEMY_API FDeckNavigationNode
{
	int32 PointId = INDEX_NONE;
	FVector LocalLocation = FVector::ZeroVector;
	TArray<int32> LinkedPointIds;
};

/** Result of one server-side deck graph search. PointIds includes start and goal. */
struct ENEMY_API FDeckNavigationPath
{
	TArray<int32> PointIds;
	int32 GoalPointId = INDEX_NONE;
	float TravelCost = 0.0f;

	bool IsValid() const
	{
		return GoalPointId != INDEX_NONE && !PointIds.IsEmpty();
	}

	void Reset()
	{
		PointIds.Reset();
		GoalPointId = INDEX_NONE;
		TravelCost = 0.0f;
	}
};

/**
 * Pure deterministic graph search. UObject state, world transforms, combat rules,
 * and reservations deliberately stay outside this type.
 */
class ENEMY_API FDeckGraphPathfinder
{
public:
	static bool FindLowestCostPathToAny(
		const TMap<int32, FDeckNavigationNode>& Nodes,
		int32 StartPointId,
		const TMap<int32, float>& GoalSecondaryCosts,
		const TSet<int32>& BlockedPointIds,
		FDeckNavigationPath& OutPath);
};
