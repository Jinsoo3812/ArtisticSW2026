#include "AI/EnemyTerritoryComponent.h"

UEnemyTerritoryComponent::UEnemyTerritoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UEnemyTerritoryComponent::InitializeTerritory(
	const FVector& InHomeLocation,
	float InPatrolRadius,
	float InCombatRadius)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	HomeLocation = InHomeLocation;
	PatrolRadius = FMath::Max(0.0f, InPatrolRadius);
	CombatRadius = FMath::Max(PatrolRadius, InCombatRadius);
	bTerritoryAssigned = true;
}

bool UEnemyTerritoryComponent::IsInsidePatrolArea(const FVector& WorldLocation) const
{
	return !bTerritoryAssigned
		|| FVector::DistSquared2D(HomeLocation, WorldLocation) <= FMath::Square(PatrolRadius);
}

bool UEnemyTerritoryComponent::IsInsideCombatArea(const FVector& WorldLocation) const
{
	return !bTerritoryAssigned
		|| FVector::DistSquared2D(HomeLocation, WorldLocation) <= FMath::Square(CombatRadius);
}
