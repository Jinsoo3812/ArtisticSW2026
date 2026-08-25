#pragma once

#include "CoreMinimal.h"

/**
 * Opaque, authority-only claim on one live deck point.
 *
 * Reservations are deliberately not replicated. The server commits them to an
 * occupant before changing replicated point IDs, so clients only observe
 * authoritative movement state.
 */
struct ENEMY_API FDeckPointReservation
{
	int32 PointId = INDEX_NONE;
	uint32 Serial = 0;
	TWeakObjectPtr<AActor> Requester;

	bool IsValid() const
	{
		return PointId != INDEX_NONE && Serial != 0 && Requester.IsValid();
	}

	void Reset()
	{
		PointId = INDEX_NONE;
		Serial = 0;
		Requester.Reset();
	}
};

/** Server-authored constraints used when a Boss requests one summon point. */
struct ENEMY_API FDeckEnemySpawnRequest
{
	TWeakObjectPtr<AActor> Requester;
	TWeakObjectPtr<AActor> Target;
	int32 ExcludedPointId = INDEX_NONE;
	TArray<int32> PreferredPointIds;
	float MinimumDistanceFromRequester = 0.0f;
	float MinimumDistanceFromTarget = 0.0f;
};

