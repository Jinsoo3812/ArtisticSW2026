#include "BossAI/BossDeckPointSelector.h"

#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "ShipAI/EnemyShip.h"

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
