#include "DeckAI/DeckEnemyNavigationComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "ShipAI/EnemyShip.h"

UDeckEnemyNavigationComponent::UDeckEnemyNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

ADeckEnemy* UDeckEnemyNavigationComponent::GetDeckEnemy() const
{
	return Cast<ADeckEnemy>(GetOwner());
}

bool UDeckEnemyNavigationComponent::BuildCombatGoals(
	AActor& TargetActor,
	bool bRequireLineOfSight,
	TMap<int32, float>& OutGoalSecondaryCosts,
	FVector& OutTargetLocalLocation) const
{
	OutGoalSecondaryCosts.Reset();
	const ADeckEnemy* Enemy = GetDeckEnemy();
	const AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	const UDeckNavigationComponent* Navigation = Ship ? Ship->GetDeckNavigationComponent() : nullptr;
	const UStaticMeshComponent* DeckMesh = Ship ? Ship->GetShipDeckMesh() : nullptr;
	if (!Enemy || !Ship || !Navigation || !DeckMesh || !Enemy->IsValidCombatTarget(&TargetActor))
	{
		return false;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	OutTargetLocalLocation = DeckTransform.InverseTransformPosition(TargetActor.GetActorLocation());
	const float MinimumRange = Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Melee
		? 0.0f
		: FMath::Max(0.0f, Enemy->GetMinAttackRange() + RangeSafetyMargin);
	const float MaximumRange = FMath::Max(MinimumRange, Enemy->GetMaxAttackRange() - RangeSafetyMargin);
	const float PreferredRange = FMath::Clamp(
		Enemy->GetPreferredDeckCombatRange(), MinimumRange, MaximumRange);

	TArray<int32> CombatPointIds;
	Ship->GetDeckWaypointIds(CombatPointIds, true);
	for (const int32 PointId : CombatPointIds)
	{
		FVector PointLocalLocation;
		if (!Navigation->GetPointLocalLocation(PointId, PointLocalLocation)
			|| !Ship->IsDeckCombatPointClaimAvailable(PointId, Enemy))
		{
			continue;
		}

		const float TargetDistance = FVector::Dist2D(PointLocalLocation, OutTargetLocalLocation);
		if (TargetDistance < MinimumRange || TargetDistance > MaximumRange
			|| (bRequireLineOfSight && !HasCandidateLineOfSight(PointId, TargetActor)))
		{
			continue;
		}

		OutGoalSecondaryCosts.Add(PointId, FMath::Abs(TargetDistance - PreferredRange));
	}
	return !OutGoalSecondaryCosts.IsEmpty();
}

bool UDeckEnemyNavigationComponent::HasCandidateLineOfSight(
	int32 PointId,
	const AActor& TargetActor) const
{
	const ADeckEnemy* Enemy = GetDeckEnemy();
	const AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UWorld* World = Ship ? Ship->GetWorld() : nullptr;
	if (!Enemy || !Ship || !World)
	{
		return false;
	}

	const float EyeHeight = Enemy->GetCapsuleComponent()
		? Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 90.0f;
	const FVector Start = Ship->GetDeckWaypointWorldLocation(PointId)
		+ Ship->GetShipDeckMesh()->GetUpVector() * EyeHeight;
	const FVector End = TargetActor.GetActorLocation() + FVector::UpVector * 60.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeckCombatCandidateLOS), true, Enemy);
	QueryParams.AddIgnoredActor(Ship);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams)
		|| Hit.GetActor() == &TargetActor;
}

bool UDeckEnemyNavigationComponent::PlanCombatRoute(
	AActor* TargetActor,
	bool bRequireLineOfSight)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UDeckNavigationComponent* Navigation = Ship ? Ship->GetDeckNavigationComponent() : nullptr;
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->IsPoolActive()
		|| !Ship || !Navigation || !Enemy->IsValidCombatTarget(TargetActor))
	{
		CancelCombatRoute();
		return false;
	}

	if (Enemy->CanAttackTarget(TargetActor, bRequireLineOfSight))
	{
		CancelCombatRoute();
		return true;
	}

	TMap<int32, float> GoalSecondaryCosts;
	FVector TargetLocalLocation;
	if (!BuildCombatGoals(
		*TargetActor, bRequireLineOfSight, GoalSecondaryCosts, TargetLocalLocation))
	{
		CancelCombatRoute();
		return false;
	}

	CancelCombatRoute();
	const int32 StartPointId = Enemy->GetCurrentDeckWaypointId();
	while (!GoalSecondaryCosts.IsEmpty())
	{
		FDeckNavigationPath CandidateRoute;
		if (!Navigation->FindPathToAny(
			StartPointId, GoalSecondaryCosts, Enemy, CandidateRoute))
		{
			return false;
		}

		if (!Ship->TryClaimDeckCombatPoint(CandidateRoute.GoalPointId, Enemy))
		{
			GoalSecondaryCosts.Remove(CandidateRoute.GoalPointId);
			continue;
		}

		Route = MoveTemp(CandidateRoute);
		RouteCursor = 0;
		ClaimedCombatPointId = Route.GoalPointId;
		PlannedGraphRevision = Navigation->GetGraphRevision();
		PlannedTargetLocalLocation = TargetLocalLocation;
		PlannedTarget = TargetActor;
		NextAllowedReplanTime = GetWorld()
			? GetWorld()->GetTimeSeconds() + FMath::Max(0.05f, MinimumReplanInterval)
			: 0.0;

		if (Route.PointIds.Num() == 1)
		{
			return true;
		}
		if (PrepareNextHop())
		{
			return true;
		}

		const int32 FailedGoalPointId = ClaimedCombatPointId;
		ReleaseCombatClaim();
		Route.Reset();
		GoalSecondaryCosts.Remove(FailedGoalPointId);
	}
	return false;
}

bool UDeckEnemyNavigationComponent::PrepareNextHop()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (!Enemy || !Route.IsValid())
	{
		return false;
	}

	const int32 CurrentPointId = Enemy->GetCurrentDeckWaypointId();
	const int32 CurrentRouteIndex = Route.PointIds.IndexOfByKey(CurrentPointId);
	if (CurrentRouteIndex == INDEX_NONE)
	{
		CancelCombatRoute();
		return false;
	}
	RouteCursor = CurrentRouteIndex;
	if (RouteCursor + 1 >= Route.PointIds.Num())
	{
		return false;
	}

	return Enemy->TrySetGoalDeckWaypointId(Route.PointIds[RouteCursor + 1]);
}

bool UDeckEnemyNavigationComponent::HandlePointReached()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (!Enemy || !Route.IsValid())
	{
		return false;
	}

	const int32 CurrentRouteIndex = Route.PointIds.IndexOfByKey(Enemy->GetCurrentDeckWaypointId());
	if (CurrentRouteIndex == INDEX_NONE)
	{
		CancelCombatRoute();
		return false;
	}
	RouteCursor = CurrentRouteIndex;
	if (RouteCursor + 1 >= Route.PointIds.Num())
	{
		return false;
	}
	if (PrepareNextHop())
	{
		return true;
	}

	// The route can no longer make forward progress. Release the final soft claim so
	// another combatant may use it while this enemy returns to selection/replanning.
	CancelCombatRoute();
	return false;
}

bool UDeckEnemyNavigationComponent::ReplanIfTargetMoved(
	AActor* TargetActor,
	bool bRequireLineOfSight)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UDeckNavigationComponent* Navigation = Ship ? Ship->GetDeckNavigationComponent() : nullptr;
	UStaticMeshComponent* DeckMesh = Ship ? Ship->GetShipDeckMesh() : nullptr;
	UWorld* World = GetWorld();
	if (!Enemy || !Ship || !Navigation || !DeckMesh || !World || !Route.IsValid()
		|| World->GetTimeSeconds() < NextAllowedReplanTime)
	{
		return false;
	}

	const FVector CurrentTargetLocal = TargetActor
		? DeckMesh->GetComponentTransform().InverseTransformPosition(TargetActor->GetActorLocation())
		: FVector::ZeroVector;
	const bool bGraphChanged = Navigation->GetGraphRevision() != PlannedGraphRevision;
	const bool bTargetChanged = TargetActor != PlannedTarget.Get()
		|| FVector::Dist2D(CurrentTargetLocal, PlannedTargetLocalLocation)
			>= FMath::Max(25.0f, TargetReplanDistance);
	if (!bGraphChanged && !bTargetChanged)
	{
		return false;
	}

	return PlanCombatRoute(TargetActor, bRequireLineOfSight);
}

bool UDeckEnemyNavigationComponent::IsAtCombatGoal() const
{
	const ADeckEnemy* Enemy = GetDeckEnemy();
	return Enemy && ClaimedCombatPointId != INDEX_NONE
		&& Enemy->GetCurrentDeckWaypointId() == ClaimedCombatPointId;
}

void UDeckEnemyNavigationComponent::ReleaseCombatClaim()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr)
	{
		Ship->ReleaseDeckCombatPointClaim(ClaimedCombatPointId, Enemy);
	}
	ClaimedCombatPointId = INDEX_NONE;
}

void UDeckEnemyNavigationComponent::CancelCombatRoute()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (Enemy && Enemy->HasAuthority())
	{
		Enemy->TrySetGoalDeckWaypointId(INDEX_NONE);
	}
	ReleaseCombatClaim();
	Route.Reset();
	RouteCursor = 0;
	PlannedGraphRevision = INDEX_NONE;
	PlannedTargetLocalLocation = FVector::ZeroVector;
	PlannedTarget.Reset();
}
