#include "BossAI/BossDeckPointSelector.h"

#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GAS/Tasks/BossTargetSnapshotTypes.h"
#include "ShipAI/EnemyShip.h"

bool UBossDeckPointSelector::SelectDashBeyondSnapshot(AShipBossEnemy& Boss,
	const FDeckWalkLocation& Start, const FBossTargetSnapshot& Target, float DistanceBeyondPlayer,
	const FBossDestinationSelectionSettings& Settings, AActor* IgnoredTarget, FDeckWalkLocation& Out)
{
	Out = FDeckWalkLocation();
	AEnemyShip* Ship = Boss.GetHostShip();
	const UDeckWalkAreaComponent* Area = IsValid(Ship) ? Ship->GetDeckWalkAreaComponent() : nullptr;
	const UStaticMeshComponent* Frame = IsValid(Ship) ? Ship->GetDeckMeshComplex() : nullptr;
	if (!Boss.HasAuthority() || !Area || !Frame || !Target.bValid || !Area->IsLocationValid(Start)
		|| Start.SurfaceId != Target.SurfaceId || !FMath::IsFinite(DistanceBeyondPlayer) || DistanceBeyondPlayer <= 0.f) return false;
	const FVector Up = Frame->GetUpVector().GetSafeNormal();
	const FVector TargetWorld = Area->ToWorld(Target.LocalFloor);
	const FVector StartFloorWorld = Area->ToWorld(Start.LocalFloor);
	const FVector Direction = FVector::VectorPlaneProject(TargetWorld - StartFloorWorld, Up).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	const FVector Desired = TargetWorld + Direction * DistanceBeyondPlayer;
	FDeckWalkLocation Goal;
	if (!Area->ResolvePreciseLocalFloor(Area->ToLocal(Desired), Target.SurfaceId, Goal)
		|| !Area->IsLocationAvailable(Goal, Boss) || !Area->IsSupportedSegment(Start, Goal)) return false;
	FTransform StartTransform, EndTransform;
	if (!Area->ResolveLocationTransform(Start, Boss, StartTransform) || !Area->ResolveLocationTransform(Goal, Boss, EndTransform)) return false;
	const float Travel = FVector::VectorPlaneProject(EndTransform.GetLocation() - StartTransform.GetLocation(), Up).Size();
	if (Travel < FMath::Max(Settings.MinimumTravelDistance, Settings.MinimumDashTravelDistance)
		|| Travel > Settings.MaximumDashDistance) return false;
	// Preserve the requested distance and line after floor resolution; no nearest-node fallback.
	if (FVector::VectorPlaneProject(Area->ToWorld(Goal.LocalFloor) - Desired, Up).Size() > 1.f) return false;
	if (Settings.bCheckDashObstacles)
	{
		const UCapsuleComponent* Capsule = Boss.GetCapsuleComponent();
		const UWorld* World = Boss.GetWorld();
		if (!Capsule || !World) return false;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BossSnapshotDashObstacle), true, &Boss);
		if (IsValid(IgnoredTarget)) Params.AddIgnoredActor(IgnoredTarget);
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, StartTransform.GetLocation(), EndTransform.GetLocation(),
			Boss.GetActorQuat(), ECC_Pawn, FCollisionShape::MakeCapsule(
				Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params)) return false;
	}
	Out = Goal;
	return true;
}

bool UBossDeckPointSelector::SelectChainRelocation(AShipBossEnemy& Boss, const FBossTargetSnapshot& Target,
	float DistanceBeyondPlayer, const FBossDestinationSelectionSettings& Settings,
	AActor* IgnoredTarget, FDeckWalkLocation& Out)
{
	Out = FDeckWalkLocation();
	AEnemyShip* Ship = Boss.GetHostShip();
	const UDeckWalkAreaComponent* Area = IsValid(Ship) ? Ship->GetDeckWalkAreaComponent() : nullptr;
	const UStaticMeshComponent* Frame = IsValid(Ship) ? Ship->GetDeckMeshComplex() : nullptr;
	FDeckWalkLocation Current;
	if (!Boss.HasAuthority() || !Area || !Frame || !Target.bValid || !Area->ResolveActorOnDeck(Boss, Current)) return false;
	const FVector TargetWorld = Area->ToWorld(Target.LocalFloor);
	const FVector Forward = Frame->GetComponentTransform().TransformVectorNoScale(Target.LocalForward).GetSafeNormal();
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Current, Candidates, false);
	float Best = TNumericLimits<float>::Max();
	for (const auto& Candidate : Candidates)
	{
		if (Candidate.SurfaceId != Target.SurfaceId || !Area->IsLocationAvailable(Candidate, Boss)) continue;
		const FVector Position = Area->ToWorld(Candidate.LocalFloor);
		if (FVector::VectorPlaneProject(Position - Boss.GetActorLocation(), Frame->GetUpVector()).Size() < Settings.MinimumTravelDistance
			|| !IsPointBehindTarget(TargetWorld, Forward, Position, Frame->GetUpVector(), Settings.MaximumRearDot)) continue;
		FDeckWalkLocation DashGoal;
		if (!SelectDashBeyondSnapshot(Boss, Candidate, Target, DistanceBeyondPlayer, Settings, IgnoredTarget, DashGoal)) continue;
		const float Score = FVector::DistSquared(Position, TargetWorld);
		if (Score < Best || (FMath::IsNearlyEqual(Score, Best) && (Out.NodeIndex == INDEX_NONE || Candidate.NodeIndex < Out.NodeIndex)))
		{
			Best = Score;
			Out = Candidate;
		}
	}
	return Area->IsLocationValid(Out);
}

bool UBossDeckPointSelector::SelectDestinationLocation(
	AEnemyShip* Ship, AActor* BossActor, AActor* Target, EBossDestinationPurpose Purpose,
	EBossDestinationRelation Relation, const FBossDestinationSelectionSettings& Settings, FDeckWalkLocation& Out)
{
	Out = FDeckWalkLocation();
	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(BossActor);
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, TargetFloor;
	if (!Ship || !Boss || !Boss->HasAuthority() || !IsValid(Target) || !Area
		|| !Area->ResolveActorOnDeck(*Boss, Start) || !Area->ResolveActorOnDeck(*Target, TargetFloor)) return false;
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates, Purpose == EBossDestinationPurpose::Walk);
	const FVector Up = Ship->GetDeckMeshComplex()->GetUpVector();
	float BestPrimary = TNumericLimits<float>::Max();
	float BestSecondary = TNumericLimits<float>::Max();
	for (const auto& Candidate : Candidates)
	{
		if (Purpose != EBossDestinationPurpose::Vanish && Candidate.SurfaceId != TargetFloor.SurfaceId) continue;
		FTransform Transform;
		if (!Area->ResolveLocationTransform(Candidate, *Boss, Transform)) continue;
		const FVector Position = Transform.GetLocation();
		const float Travel = FVector::Dist2D(Candidate.LocalFloor, Start.LocalFloor);
		const float MinimumTravel = Purpose == EBossDestinationPurpose::Dash
			? FMath::Max(Settings.MinimumTravelDistance, Settings.MinimumDashTravelDistance) : Settings.MinimumTravelDistance;
		if ((Purpose == EBossDestinationPurpose::Walk ? Candidate.NodeIndex == Start.NodeIndex : Travel < MinimumTravel)
			|| !Area->IsLocationAvailable(Candidate, *Boss)) continue;
		if (Purpose == EBossDestinationPurpose::Vanish && Relation != EBossDestinationRelation::Any
			&& !(Relation == EBossDestinationRelation::BehindTarget
				? IsPointBehindTarget(Target->GetActorLocation(), Target->GetActorForwardVector(), Position, Up, Settings.MaximumRearDot)
				: IsPointInFrontOfTarget(Target->GetActorLocation(), Target->GetActorForwardVector(), Position, Up, Settings.MinimumFrontDot))) continue;
		float Primary = 0.0f, Secondary = 0.0f;
		if (Purpose == EBossDestinationPurpose::Dash)
		{
			if (!Area->IsSupportedSegment(Start, Candidate) || !IsDashSegmentClear(*Ship, *Boss, *Target, Position, Settings)) continue;
			const FVector Closest = FMath::ClosestPointOnSegment(Target->GetActorLocation(), Boss->GetActorLocation(), Position);
			Primary = FVector::VectorPlaneProject(Target->GetActorLocation() - Closest, Up).SizeSquared();
			Secondary = Settings.PreferredDashTravelDistance > 0.0f
				? FMath::Abs(Travel - Settings.PreferredDashTravelDistance) : FVector::DistSquared(Position, Target->GetActorLocation());
		}
		else if (Purpose == EBossDestinationPurpose::Walk)
		{
			const auto& Previous = Boss->GetPreviousLocation();
			if (Area->IsLocationValid(Previous) && Previous.NodeIndex == Candidate.NodeIndex) continue;
			Primary = FMath::Abs(FVector::Dist2D(Candidate.LocalFloor, TargetFloor.LocalFloor) - Settings.IdealWalkRange);
			TArray<FDeckWalkLocation> Path;
			if (!Area->FindPath(Start, Candidate, Path)) continue;
			for (int32 I = 1; I < Path.Num(); ++I) Secondary += FVector::Dist(Path[I - 1].LocalFloor, Path[I].LocalFloor);
		}
		else Primary = FVector::DistSquared(Position, Target->GetActorLocation());
		if (Primary < BestPrimary - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(Primary, BestPrimary)
			&& (Secondary < BestSecondary - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(Secondary, BestSecondary)
				&& (Out.NodeIndex == INDEX_NONE || Candidate.NodeIndex < Out.NodeIndex)))))
		{
			BestPrimary = Primary; BestSecondary = Secondary; Out = Candidate;
		}
	}
	return Area->IsLocationValid(Out);
}

bool UBossDeckPointSelector::IsPointBehindTarget(
	const FVector& TargetLocation,
	const FVector& TargetForward,
	const FVector& PointLocation,
	const FVector& DeckUp,
	float MaximumRearDot)
{
	const FVector SafeUp = DeckUp.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
	const FVector ForwardOnDeck = FVector::VectorPlaneProject(TargetForward, SafeUp).GetSafeNormal();
	const FVector TargetToPointOnDeck = FVector::VectorPlaneProject(
		PointLocation - TargetLocation,
		SafeUp).GetSafeNormal();
	if (ForwardOnDeck.IsNearlyZero() || TargetToPointOnDeck.IsNearlyZero())
	{
		return false;
	}

	return FVector::DotProduct(ForwardOnDeck, TargetToPointOnDeck)
		<= FMath::Clamp(MaximumRearDot, -1.0f, 0.0f);
}

bool UBossDeckPointSelector::IsPointInFrontOfTarget(
	const FVector& TargetLocation,
	const FVector& TargetForward,
	const FVector& PointLocation,
	const FVector& DeckUp,
	float MinimumFrontDot)
{
	const FVector SafeUp = DeckUp.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
	const FVector ForwardOnDeck = FVector::VectorPlaneProject(TargetForward, SafeUp).GetSafeNormal();
	const FVector TargetToPointOnDeck = FVector::VectorPlaneProject(
		PointLocation - TargetLocation,
		SafeUp).GetSafeNormal();
	if (ForwardOnDeck.IsNearlyZero() || TargetToPointOnDeck.IsNearlyZero())
	{
		return false;
	}

	return FVector::DotProduct(ForwardOnDeck, TargetToPointOnDeck)
		>= FMath::Clamp(MinimumFrontDot, 0.0f, 1.0f);
}

bool UBossDeckPointSelector::DoesSegmentPassTarget(
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	const FVector& TargetLocation,
	float CorridorRadius)
{
	if (FVector::DistSquared(SegmentStart, SegmentEnd) <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ClosestPoint = FMath::ClosestPointOnSegment(TargetLocation, SegmentStart, SegmentEnd);
	return FVector::DistSquared(ClosestPoint, TargetLocation)
		<= FMath::Square(FMath::Max(1.0f, CorridorRadius));
}

bool UBossDeckPointSelector::IsDashSegmentClear(
	const AEnemyShip& HostShip,
	const AActor& BossActor,
	const AActor& TargetActor,
	const FVector& Destination,
	const FBossDestinationSelectionSettings& Settings)
{
	const FVector Start = BossActor.GetActorLocation();
	const FVector DeckUp = HostShip.GetShipDeckMesh()
		? HostShip.GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	if (FVector::VectorPlaneProject(Destination - Start, DeckUp).SizeSquared()
		> FMath::Square(FMath::Max(1.0f, Settings.MaximumDashDistance)))
	{
		return false;
	}

	if (Settings.bRequireDashPathThroughTarget
		&& !DoesSegmentPassTarget(
			Start, Destination, TargetActor.GetActorLocation(), Settings.DashHitCorridorRadius))
	{
		return false;
	}

	if (!Settings.bCheckDashObstacles)
	{
		return true;
	}

	const UWorld* World = HostShip.GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BossDashObstacle), true, &BossActor);
	QueryParams.AddIgnoredActor(&BossActor);
	QueryParams.AddIgnoredActor(&TargetActor);
	FHitResult Hit;
	const ACharacter* Character = Cast<ACharacter>(&BossActor);
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	return Capsule && !World->SweepSingleByChannel(Hit, Start, Destination, BossActor.GetActorQuat(), ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), QueryParams);
}
