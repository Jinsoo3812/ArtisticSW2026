#include "DeckAI/DeckEnemyNavigationComponent.h"

#include "Components/CapsuleComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "ShipAI/EnemyShip.h"

UDeckEnemyNavigationComponent::UDeckEnemyNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
ADeckEnemy* UDeckEnemyNavigationComponent::GetDeckEnemy() const { return Cast<ADeckEnemy>(GetOwner()); }

FVector UDeckEnemyNavigationComponent::CalculateDistanceGoal(const FVector& PlayerFloor, const FVector& EnemyFloor,
	float Distance, float AngleDegrees)
{
	FVector Direction = FVector(EnemyFloor.X - PlayerFloor.X, EnemyFloor.Y - PlayerFloor.Y, 0.0f).GetSafeNormal();
	if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;
	return PlayerFloor + Direction.RotateAngleAxis(AngleDegrees, FVector::UpVector) * FMath::Max(0.0f, Distance);
}

bool UDeckEnemyNavigationComponent::ClaimGoal(const FDeckWalkLocation& Goal)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->GetDeckWalkRouteComponent() : nullptr;
	if (!Area || !Route || !Enemy->CanMoveOnDeck() || !Area->TryClaimLocation(Goal, *Enemy)
		|| !(bUseDistanceBand ? Route->SetLocationGoalInDistanceBand(Goal, BandCenter, BandDistance, 100.0f)
			: Route->SetLocationGoal(Goal)))
	{
		if (Area) Area->ReleaseLocationClaim(Enemy);
		if (Route) Route->ClearGoal();
		return false;
	}
	CombatGoal = Goal;
	Enemy->BeginFreeDeckMovement();
	return true;
}

bool UDeckEnemyNavigationComponent::SelectNearGoal(const FDeckWalkLocation& Start, const FVector& Ideal,
	FName Surface, float Tolerance, bool bExcludePlayer, AActor* Player)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	if (!Area || !Enemy->CanMoveOnDeck() || Ideal.ContainsNaN() || !FMath::IsFinite(Tolerance)) return false;
	auto IsUsable = [&](const FDeckWalkLocation& Goal)
	{
		if (!Area->IsLocationAvailable(Goal, *Enemy)) return false;
		if (bExcludePlayer && Player)
		{
			const ACharacter* Character = Cast<ACharacter>(Player);
			const float OtherRadius = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 45.0f;
			if (FVector::Dist2D(Goal.LocalFloor, Area->ToLocal(Area->GetActorFeetWorld(*Player)))
				< Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + OtherRadius + 10.0f) return false;
		}
		return true;
	};
	FDeckWalkLocation Exact;
	if (Area->ResolvePreciseLocalFloor(Ideal, Surface, Exact) && IsUsable(Exact) && ClaimGoal(Exact)) return true;
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates);
	Candidates.RemoveAll([&](const FDeckWalkLocation& Candidate)
	{
		return Candidate.SurfaceId != Surface || FVector::Dist2D(Candidate.LocalFloor, Ideal) > FMath::Max(0.0f, Tolerance);
	});
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{
		const float DA = FVector::DistSquared2D(A.LocalFloor, Ideal), DB = FVector::DistSquared2D(B.LocalFloor, Ideal);
		return FMath::IsNearlyEqual(DA, DB) ? A.NodeIndex < B.NodeIndex : DA < DB;
	});
	for (const FDeckWalkLocation& Candidate : Candidates)
	{
		if (Candidate.SurfaceId != Surface || FVector::Dist2D(Candidate.LocalFloor, Ideal) > FMath::Max(0.0f, Tolerance)) continue;
		if (IsUsable(Candidate) && ClaimGoal(Candidate)) return true;
	}
	return false;
}

bool UDeckEnemyNavigationComponent::PlanTargetDistanceRoute(AActor* Target, float Distance, float ProjectionTolerance, float AngleDegrees)
{
	CancelCombatRoute();
	ADeckEnemy* Enemy = GetDeckEnemy();
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, TargetFloor;
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Area || !Enemy->IsValidCombatTarget(Target)
		|| !FMath::IsFinite(Distance) || !FMath::IsFinite(AngleDegrees)
		|| !Area->ResolveActorOnDeck(*Enemy, Start) || !Area->ResolveActorOnDeck(*Target, TargetFloor)) return false;
	const FVector TargetFeet = Area->ToLocal(Area->GetActorFeetWorld(*Target));
	const FVector SelfFeet = Area->ToLocal(Area->GetActorFeetWorld(*Enemy));
	// First acquire the radius; lateral paths stay in the annulus and cannot cut through the Player.
	bUseDistanceBand = !FMath::IsNearlyZero(AngleDegrees) && Start.SurfaceId == TargetFloor.SurfaceId
		&& FMath::Abs(FVector::Dist2D(SelfFeet, TargetFeet) - Distance) <= 100.0f;
	BandCenter = TargetFeet; BandDistance = Distance;
	FVector Ideal = CalculateDistanceGoal(TargetFeet, SelfFeet, Distance, bUseDistanceBand ? AngleDegrees : 0.0f);
	if (!SelectNearGoal(Start, Ideal, TargetFloor.SurfaceId, ProjectionTolerance, true, Target)) { CancelCombatRoute(); return false; }
	PlannedTarget = Target; PlannedTargetFloor = TargetFloor; PlannedTargetFloor.LocalFloor = TargetFeet;
	PlannedDistance = Distance; PlannedAngle = AngleDegrees; PlannedProjectionTolerance = ProjectionTolerance;
	NextAllowedReplanTime = GetWorld()->GetTimeSeconds() + MinimumReplanInterval;
	return true;
}

bool UDeckEnemyNavigationComponent::PlanRecoveryRoute(AActor* Target)
{
	CancelCombatRoute();
	ADeckEnemy* Enemy = GetDeckEnemy();
	UDeckEnemyCombatComponent* Combat = Enemy ? Enemy->GetDeckCombatComponent() : nullptr;
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, Snapshot;
	if (!Combat || !Area || !Combat->HasRecovery(Target) || !Combat->GetRecoveryGoal(Snapshot)
		|| !Area->ResolveActorOnDeck(*Enemy, Start)) return false;
	// A local point survives ship motion and a graph rebuild. SelectNearGoal resolves a fresh handle.
	if (!SelectNearGoal(Start, Snapshot.LocalFloor, Snapshot.SurfaceId, 250.0f, true, Target)) return false;
	PlannedTarget = Target; bRecoveryRoute = true;
	return true;
}

bool UDeckEnemyNavigationComponent::PlanInvestigationRoute(const FVector& WorldPoint)
{
	CancelCombatRoute();
	ADeckEnemy* Enemy = GetDeckEnemy();
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start;
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Area || WorldPoint.ContainsNaN() || !Area->ResolveActorOnDeck(*Enemy, Start)) return false;
	const FVector Local = Area->ToLocal(WorldPoint);
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates);
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{
		return FVector::DistSquared(A.LocalFloor, Local) < FVector::DistSquared(B.LocalFloor, Local);
	});
	for (const FDeckWalkLocation& Candidate : Candidates)
	{
		if (FVector::Dist2D(Candidate.LocalFloor, Local) > 250.0f || FMath::Abs(Candidate.LocalFloor.Z - Local.Z) > 120.0f) continue;
		if (ClaimGoal(Candidate)) return true;
	}
	return false;
}

bool UDeckEnemyNavigationComponent::ReplanIfTargetMoved(AActor* Target)
{
	if (!HasActiveRoute() || bRecoveryRoute || !PlannedTarget.IsValid() || Target != PlannedTarget.Get()
		|| GetWorld()->GetTimeSeconds() < NextAllowedReplanTime) return false;
	ADeckEnemy* Enemy = GetDeckEnemy();
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Floor;
	if (!Area || !Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Target, Floor)) { CancelCombatRoute(); return true; }
	const FVector Feet = Area->ToLocal(Area->GetActorFeetWorld(*Target));
	if (Area->IsLocationValid(CombatGoal) && Floor.SurfaceId == PlannedTargetFloor.SurfaceId
		&& FMath::Abs(Feet.Z - PlannedTargetFloor.LocalFloor.Z) <= 45.0f
		&& FVector::Dist2D(Feet, PlannedTargetFloor.LocalFloor) < TargetReplanDistance) return false;
	PlanTargetDistanceRoute(Target, PlannedDistance, PlannedProjectionTolerance, PlannedAngle);
	return true;
}

void UDeckEnemyNavigationComponent::CancelCombatRoute()
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	if (!Enemy || !Enemy->HasAuthority()) return;
	if (Enemy->GetDeckWalkRouteComponent()) Enemy->GetDeckWalkRouteComponent()->ClearGoal();
	if (AEnemyShip* Ship = Enemy->GetDeckHostShip(); Ship && Ship->GetDeckWalkAreaComponent()) Ship->GetDeckWalkAreaComponent()->ReleaseLocationClaim(Enemy);
	CombatGoal = FDeckWalkLocation(); PlannedTarget.Reset(); bRecoveryRoute = bUseDistanceBand = false;
}
