#include "DeckAI/DeckEnemyNavigationComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "Engine/World.h"
#include "ShipAI/EnemyShip.h"

UDeckEnemyNavigationComponent::UDeckEnemyNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
ADeckEnemy* UDeckEnemyNavigationComponent::GetDeckEnemy() const { return Cast<ADeckEnemy>(GetOwner()); }

bool UDeckEnemyNavigationComponent::HasCandidateLineOfSight(const FDeckWalkLocation& Location, const AActor& Target) const
{
	const ADeckEnemy* Enemy = GetDeckEnemy();
	const AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	if (!Area || !Ship->GetDeckMeshComplex() || !Enemy->GetCapsuleComponent()) return false;
	const FVector Start = Area->ToWorld(Location.LocalFloor)
		+ Ship->GetDeckMeshComplex()->GetUpVector() * Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return Enemy->TraceLineOfSightFrom(&Target, Start, Enemy->GetRangedAimLocation(&Target));
}

bool UDeckEnemyNavigationComponent::PlanCombatRoute(AActor* Target, bool bRequireLineOfSight)
{
	CancelCombatRoute();
	ADeckEnemy* Enemy = GetDeckEnemy();
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->GetDeckWalkRouteComponent() : nullptr;
	FDeckWalkLocation Start, TargetFloor;
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->IsPoolActive() || !Area || !Route
		|| !Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Enemy, Start)
		|| !Area->ResolveActorOnDeck(*Target, TargetFloor)) return false;
	if (Enemy->CanAttackTarget(Target, bRequireLineOfSight)) return true;
	Enemy->BeginFreeDeckMovement();
	if (Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Melee) return Route->SetActorGoal(Target);

	const float MinRange = FMath::Max(0.0f, Enemy->GetMinAttackRange() + RangeSafetyMargin);
	const float MaxRange = FMath::Max(MinRange, Enemy->GetMaxAttackRange() - RangeSafetyMargin);
	const float Preferred = FMath::Clamp(Enemy->GetPreferredDeckCombatRange(), MinRange, MaxRange);
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates);
	float BestTravel = TNumericLimits<float>::Max();
	float BestRange = TNumericLimits<float>::Max();
	FDeckWalkLocation Best;
	for (const auto& Candidate : Candidates)
	{
		if (Candidate.SurfaceId != TargetFloor.SurfaceId) continue;
		const float Distance = FVector::Dist2D(Candidate.LocalFloor, TargetFloor.LocalFloor);
		const float RangeCost = FMath::Abs(Distance - Preferred);
		if (Distance < MinRange || Distance > MaxRange
			|| FVector::Dist(Start.LocalFloor, Candidate.LocalFloor) > BestTravel + KINDA_SMALL_NUMBER
			|| !Area->IsLocationAvailable(Candidate, *Enemy)
			|| (bRequireLineOfSight && !HasCandidateLineOfSight(Candidate, *Target))) continue;
		TArray<FDeckWalkLocation> Path;
		if (!Area->FindPath(Start, Candidate, Path)) continue;
		float Cost = 0.0f;
		for (int32 I = 1; I < Path.Num(); ++I) Cost += FVector::Dist(Path[I - 1].LocalFloor, Path[I].LocalFloor);
		if (Cost < BestTravel - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(Cost, BestTravel)
			&& (RangeCost < BestRange - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(RangeCost, BestRange)
				&& (Best.NodeIndex == INDEX_NONE || Candidate.NodeIndex < Best.NodeIndex)))))
		{
			Best = Candidate; BestTravel = Cost; BestRange = RangeCost;
		}
	}
	if (!Area->IsLocationValid(Best) || !Area->TryClaimLocation(Best, *Enemy) || !Route->SetLocationGoal(Best))
	{
		CancelCombatRoute();
		return false;
	}
	CombatGoal = Best;
	PlannedTargetFloor = TargetFloor;
	PlannedTarget = Target;
	NextAllowedReplanTime = GetWorld()->GetTimeSeconds() + MinimumReplanInterval;
	return true;
}

bool UDeckEnemyNavigationComponent::ReplanIfTargetMoved(AActor* Target, bool bRequireLineOfSight)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation TargetFloor;
	if (!HasActiveRoute() || !Area || !GetWorld() || GetWorld()->GetTimeSeconds() < NextAllowedReplanTime) return false;
	if (!Target || !Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Target, TargetFloor))
	{
		CancelCombatRoute();
		return true;
	}
	if (Area->IsLocationValid(CombatGoal) && Target == PlannedTarget.Get()
		&& TargetFloor.SurfaceId == PlannedTargetFloor.SurfaceId
		&& FMath::Abs(TargetFloor.LocalFloor.Z - PlannedTargetFloor.LocalFloor.Z) <= 45.0f
		&& FVector::Dist2D(TargetFloor.LocalFloor, PlannedTargetFloor.LocalFloor) < TargetReplanDistance) return false;
	PlanCombatRoute(Target, bRequireLineOfSight);
	return true;
}

void UDeckEnemyNavigationComponent::RequestReleaseLineOfSightReposition(AActor* Target)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->IsPoolActive()
		|| Enemy->GetDeckCombatRole() != EDeckEnemyCombatRole::Ranged || !Enemy->IsValidCombatTarget(Target)) return;
	ReleaseLineOfSightRepositionState = EReleaseLineOfSightRepositionState::Pending;
	ReleaseLineOfSightRepositionTarget = Target;
}
bool UDeckEnemyNavigationComponent::PrepareReleaseLineOfSightReposition(AActor* Target)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->GetDeckWalkRouteComponent() : nullptr;
	FDeckWalkLocation Start, TargetFloor;
	if (!Enemy || !Enemy->HasAuthority() || !Area || !Route || !HasReleaseLineOfSightReposition(Target)
		|| !Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Enemy, Start)
		|| !Area->ResolveActorOnDeck(*Target, TargetFloor)) { CancelCombatRoute(); return false; }
	if (ReleaseLineOfSightRepositionState == EReleaseLineOfSightRepositionState::Moving) return Route->HasGoal();
	CancelRouteState();
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates);
	while (!Candidates.IsEmpty())
	{
		const int32 Choice = Enemy->GetDeckRandomStream().RandRange(0, Candidates.Num() - 1);
		const auto Candidate = Candidates[Choice];
		Candidates.RemoveAtSwap(Choice, 1, EAllowShrinking::No);
		if (Candidate.SurfaceId != TargetFloor.SurfaceId || FVector::Dist2D(Candidate.LocalFloor, Start.LocalFloor) < 100.0f
			|| !Area->TryClaimLocation(Candidate, *Enemy)) continue;
		if (!Route->SetLocationGoal(Candidate)) { Area->ReleaseLocationClaim(Enemy); continue; }
		Enemy->BeginFreeDeckMovement();
		ReleaseLineOfSightRepositionState = EReleaseLineOfSightRepositionState::Moving;
		return true;
	}
	return false;
}
bool UDeckEnemyNavigationComponent::HasReleaseLineOfSightReposition(const AActor* Target) const
{
	return ReleaseLineOfSightRepositionState != EReleaseLineOfSightRepositionState::None
		&& ReleaseLineOfSightRepositionTarget.IsValid() && (!Target || Target == ReleaseLineOfSightRepositionTarget.Get());
}
void UDeckEnemyNavigationComponent::CompleteReleaseLineOfSightReposition()
{
	ReleaseLineOfSightRepositionState = EReleaseLineOfSightRepositionState::None;
	ReleaseLineOfSightRepositionTarget.Reset();
}
void UDeckEnemyNavigationComponent::CancelRouteState()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (Enemy && Enemy->HasAuthority())
	{
		if (Enemy->GetDeckWalkRouteComponent()) Enemy->GetDeckWalkRouteComponent()->ClearGoal();
		if (AEnemyShip* Ship = Enemy->GetDeckHostShip(); Ship && Ship->GetDeckWalkAreaComponent())
			Ship->GetDeckWalkAreaComponent()->ReleaseLocationClaim(Enemy);
	}
	CombatGoal = FDeckWalkLocation();
	PlannedTargetFloor = FDeckWalkLocation();
	PlannedTarget.Reset();
}
void UDeckEnemyNavigationComponent::CancelCombatRoute() { CancelRouteState(); CompleteReleaseLineOfSightReposition(); }
