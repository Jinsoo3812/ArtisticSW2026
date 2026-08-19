#pragma once

#include "CoreMinimal.h"

namespace BossDeckMovement
{
	/** Keeps an acceptance radius useful without allowing a move to succeed at its start. */
	ENEMY_API float ResolveAcceptanceRadius(
		float RequestedRadius,
		float TravelDistance,
		float MaximumTravelFraction = 0.45f);

	/** Arrival on a moving deck is intentionally measured in deck-local XY space. */
	ENEMY_API bool IsWithinPlanarAcceptance(
		const FVector& CurrentLocalLocation,
		const FVector& GoalLocalLocation,
		float AcceptanceRadius);
}
