#include "BossAI/BossDeckMovementUtils.h"

float BossDeckMovement::ResolveAcceptanceRadius(
	float RequestedRadius,
	float TravelDistance,
	float MaximumTravelFraction)
{
	const float SafeDistance = FMath::Max(0.0f, TravelDistance);
	const float SafeFraction = FMath::Clamp(MaximumTravelFraction, 0.0f, 0.95f);
	return FMath::Clamp(RequestedRadius, 0.0f, SafeDistance * SafeFraction);
}

bool BossDeckMovement::IsWithinPlanarAcceptance(
	const FVector& CurrentLocalLocation,
	const FVector& GoalLocalLocation,
	float AcceptanceRadius)
{
	const FVector2D Current2D(CurrentLocalLocation.X, CurrentLocalLocation.Y);
	const FVector2D Goal2D(GoalLocalLocation.X, GoalLocalLocation.Y);
	return FVector2D::DistSquared(Current2D, Goal2D)
		<= FMath::Square(FMath::Max(0.0f, AcceptanceRadius));
}
