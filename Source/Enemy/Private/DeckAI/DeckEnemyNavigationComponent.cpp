#include "DeckAI/DeckEnemyNavigationComponent.h"

#include "Components/CapsuleComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Weapon/BaseWeaponComponent.h"

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
	if (!Area || !Route || !Enemy->CanMoveOnDeck() || !Area->TryClaimLocation(Goal, *Enemy)) return false;
	if (!(bUseDistanceBand ? Route->SetLocationGoalInDistanceBand(Goal, BandCenter, BandDistance, 100.0f)
		: Route->SetLocationGoal(Goal)))
	{
		Area->RestoreLocationClaim(CombatGoal, *Enemy);
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
	FDeckTargetAnchor Anchor;
	const bool bScoreAttackPosition = Player && UDeckCombatTargetResolverComponent::ResolveFor(Enemy, Player, Anchor)
		&& (bRecoveryRoute || Anchor.Source != EDeckTargetAnchorSource::DeckFloor);
	FDeckWalkLocation Exact;
	if (!bScoreAttackPosition && Area->ResolvePreciseLocalFloor(Ideal, Surface, Exact) && IsUsable(Exact) && ClaimGoal(Exact)) return true;
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates);
	Candidates.RemoveAll([&](const FDeckWalkLocation& Candidate)
	{
		return Candidate.SurfaceId != Surface || FVector::Dist2D(Candidate.LocalFloor, Ideal) > FMath::Max(0.0f, Tolerance);
	});
	TMap<int32, float> CandidateScores;
	const UBaseWeaponComponent* Weapon = Enemy->GetWeaponComponent();
	const float MaximumRange = Weapon ? Weapon->GetCurrentAttackRange() : 0.f;
	const float MinimumRange = Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Melee ? 0.f : Enemy->GetMinAttackRange();
	for (const auto& Candidate : Candidates)
	{
		float Score = FVector::DistSquared2D(Candidate.LocalFloor, Ideal);
		if (bScoreAttackPosition)
		{
			const FVector Position = Area->ToWorld(Candidate.LocalFloor)
				+ Enemy->GetActorUpVector() * Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (!Enemy->TraceLineOfSightFrom(Player, Position, Enemy->GetRangedAimLocation(Player)))
				Score += FMath::Square(FMath::Max(75.f, Tolerance));
			if (MaximumRange > 0.f)
			{
				const float Distance = FVector::Distance(Position, Player->GetActorLocation());
				const float RangeError = FMath::Max(0.f, MinimumRange - Distance) + FMath::Max(0.f, Distance - MaximumRange);
				Score += FMath::Square(RangeError);
			}
			// Prefer retaining the claim when two attack positions are nearly equivalent.
			Score += Candidate.NodeIndex == CombatGoal.NodeIndex && Area->IsLocationValid(CombatGoal) ? 0.f : 100.f;
		}
		CandidateScores.Add(Candidate.NodeIndex, Score);
	}
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{
		const float DA = CandidateScores[A.NodeIndex], DB = CandidateScores[B.NodeIndex];
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
	ADeckEnemy* Enemy = GetDeckEnemy();
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start;
	FDeckTargetAnchor TargetFloor;
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Area || !Enemy->IsValidCombatTarget(Target)
		|| !FMath::IsFinite(Distance) || !FMath::IsFinite(AngleDegrees)
		|| !Area->ResolveActorOnDeck(*Enemy, Start)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(Enemy, Target, TargetFloor)) return false;
	if (PlannedTarget.IsValid() && PlannedTarget != Target) CancelCombatRoute();
	const FVector TargetFeet = TargetFloor.LocalCenter;
	const FVector SelfFeet = Area->ToLocal(Area->GetActorFeetWorld(*Enemy));
	const bool bOldBand = bUseDistanceBand;
	const FVector OldCenter = BandCenter;
	const float OldDistance = BandDistance;
	// First acquire the radius; lateral paths stay in the annulus and cannot cut through the Player.
	bUseDistanceBand = !FMath::IsNearlyZero(AngleDegrees) && Start.SurfaceId == TargetFloor.SurfaceId
		&& FMath::Abs(FVector::Dist2D(SelfFeet, TargetFeet) - Distance) <= 100.0f;
	BandCenter = TargetFeet; BandDistance = Distance;
	FVector Ideal = CalculateDistanceGoal(TargetFeet, SelfFeet, Distance, bUseDistanceBand ? AngleDegrees : 0.0f);
	bool bSelected = SelectNearGoal(Start, Ideal, TargetFloor.SurfaceId, ProjectionTolerance, true, Target);
	// At a rail only one side of the annulus may exist; try the opposite side before normal safe approach.
	if (!bSelected && bUseDistanceBand)
	{
		Ideal = CalculateDistanceGoal(TargetFeet, SelfFeet, Distance, -AngleDegrees);
		bSelected = SelectNearGoal(Start, Ideal, TargetFloor.SurfaceId, ProjectionTolerance, true, Target);
	}
	if (!bSelected && TargetFloor.Source != EDeckTargetAnchorSource::DeckFloor)
	{
		bUseDistanceBand = false;
		Ideal = CalculateDistanceGoal(TargetFeet, SelfFeet, Distance);
		bSelected = SelectNearGoal(Start, Ideal, TargetFloor.SurfaceId,
			FMath::Max(ProjectionTolerance, SupportedTargetGoalTolerance), true, Target);
	}
	if (!bSelected)
	{
		bUseDistanceBand = bOldBand; BandCenter = OldCenter; BandDistance = OldDistance;
		return false;
	}
	PlannedTarget = Target; PlannedTargetFloor = FDeckWalkLocation();
	PlannedTargetFloor.SurfaceId = TargetFloor.SurfaceId; PlannedTargetFloor.LocalFloor = TargetFeet;
	bRecoveryRoute = false;
	PlannedDistance = Distance; PlannedAngle = AngleDegrees; PlannedProjectionTolerance = ProjectionTolerance;
	NextAllowedReplanTime = GetWorld()->GetTimeSeconds() + MinimumReplanInterval;
	return true;
}

bool UDeckEnemyNavigationComponent::PlanRecoveryRoute(AActor* Target)
{
	ADeckEnemy* Enemy = GetDeckEnemy();
	UDeckEnemyCombatComponent* Combat = Enemy ? Enemy->GetDeckCombatComponent() : nullptr;
	const UDeckWalkAreaComponent* Area = Enemy && Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, Snapshot;
	if (!Combat || !Area || !Combat->HasRecovery(Target) || !Combat->GetRecoveryGoal(Snapshot)
		|| !Area->ResolveActorOnDeck(*Enemy, Start)) return false;
	// A local point survives ship motion and a graph rebuild. SelectNearGoal resolves a fresh handle.
	const bool bOldBand = bUseDistanceBand, bOldRecovery = bRecoveryRoute; bUseDistanceBand = false; bRecoveryRoute = true;
	if (!SelectNearGoal(Start, Snapshot.LocalFloor, Snapshot.SurfaceId, SupportedTargetGoalTolerance, true, Target))
	{ bUseDistanceBand = bOldBand; bRecoveryRoute = bOldRecovery; return false; }
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
	FDeckTargetAnchor Floor;
	if (!Area || !Enemy->IsValidCombatTarget(Target)) { CancelCombatRoute(); return true; }
	NextAllowedReplanTime = GetWorld()->GetTimeSeconds() + MinimumReplanInterval;
	if (!UDeckCombatTargetResolverComponent::ResolveFor(Enemy, Target, Floor)) return false;
	const FVector Feet = Floor.LocalCenter;
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
