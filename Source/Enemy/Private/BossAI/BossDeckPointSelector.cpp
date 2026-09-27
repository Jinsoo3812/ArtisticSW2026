#include "BossAI/BossDeckPointSelector.h"

#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "ShipAI/EnemyShip.h"

bool UBossDeckPointSelector::SelectDestinationPoint(
	AEnemyShip* HostShip,
	AActor* BossActor,
	AActor* TargetActor,
	EBossDestinationPurpose Purpose,
	EBossDestinationRelation Relation,
	const FBossDestinationSelectionSettings& Settings,
	int32& OutPointId)
{
	OutPointId = INDEX_NONE;
	if (!IsValid(HostShip) || !IsValid(BossActor) || !IsValid(TargetActor))
	{
		return false;
	}
	if (Purpose == EBossDestinationPurpose::Walk)
	{
		return SelectWalkDestinationPoint(
			*HostShip, *BossActor, *TargetActor, Settings, OutPointId);
	}

	const FVector DeckUp = HostShip->GetShipDeckMesh()
		? HostShip->GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	const FVector BossLocation = BossActor->GetActorLocation();
	const FVector TargetLocation = TargetActor->GetActorLocation();

	TArray<int32> CandidateIds;
	HostShip->GetDeckWaypointIds(CandidateIds, true);

	float BestPathTargetDistanceSquared = TNumericLimits<float>::Max();
	float BestPreferredDistanceError = TNumericLimits<float>::Max();
	float BestTargetDistanceSquared = TNumericLimits<float>::Max();
	for (const int32 CandidateId : CandidateIds)
	{
		const UDeckWaypointComponent* Waypoint = HostShip->GetDeckWaypoint(CandidateId);
		if (!IsValid(Waypoint) || !HostShip->IsDeckPointAvailable(CandidateId, BossActor))
		{
			continue;
		}

		FVector CandidateLocation = Waypoint->GetComponentLocation();
		if (const ACharacter* BossCharacter = Cast<ACharacter>(BossActor))
		{
			const UCapsuleComponent* Capsule = BossCharacter->GetCapsuleComponent();
			FTransform CharacterTransform;
			if (HostShip->ResolveDeckCharacterTransform(
				CandidateId,
				Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f,
				CharacterTransform))
			{
				CandidateLocation = CharacterTransform.GetLocation();
			}
		}
		const FVector BossToCandidateOnDeck = FVector::VectorPlaneProject(
			CandidateLocation - BossLocation,
			DeckUp);
		const float TravelDistance = BossToCandidateOnDeck.Size();
		const float MinimumTravelDistance = Purpose == EBossDestinationPurpose::Dash
			? FMath::Max(Settings.MinimumTravelDistance, Settings.MinimumDashTravelDistance)
			: Settings.MinimumTravelDistance;
		if (TravelDistance < FMath::Max(0.0f, MinimumTravelDistance))
		{
			continue;
		}

		const bool bMatchesTargetRelation = Purpose == EBossDestinationPurpose::Dash
			|| Relation == EBossDestinationRelation::Any
			|| (Relation == EBossDestinationRelation::BehindTarget
			? IsPointBehindTarget(
				TargetLocation,
				TargetActor->GetActorForwardVector(),
				CandidateLocation,
				DeckUp,
				Settings.MaximumRearDot)
			: IsPointInFrontOfTarget(
				TargetLocation,
				TargetActor->GetActorForwardVector(),
				CandidateLocation,
				DeckUp,
				Settings.MinimumFrontDot));
		if (!bMatchesTargetRelation)
		{
			continue;
		}

		if (Settings.bCheckDestinationOccupancy
			&& !IsDestinationClear(*HostShip, *BossActor, CandidateLocation, TargetActor))
		{
			continue;
		}

		if (Purpose == EBossDestinationPurpose::Dash
			&& !IsDashSegmentClear(*HostShip, *BossActor, *TargetActor, CandidateLocation, Settings))
		{
			continue;
		}

		const FVector ClosestPathPoint = FMath::ClosestPointOnSegment(
			TargetLocation, BossLocation, CandidateLocation);
		const float PathTargetDistanceSquared = Purpose == EBossDestinationPurpose::Dash
			? FVector::VectorPlaneProject(TargetLocation - ClosestPathPoint, DeckUp).SizeSquared()
			: 0.0f;
		const float PreferredDistanceError = Purpose == EBossDestinationPurpose::Dash
			&& Settings.PreferredDashTravelDistance > 0.0f
			? FMath::Abs(TravelDistance - Settings.PreferredDashTravelDistance)
			: 0.0f;
		const float TargetDistanceSquared = FVector::DistSquared(TargetLocation, CandidateLocation);
		const bool bBetterPath = PathTargetDistanceSquared < BestPathTargetDistanceSquared - 1.0f;
		const bool bPathTie = FMath::IsNearlyEqual(
			PathTargetDistanceSquared, BestPathTargetDistanceSquared, 1.0f);
		const bool bBetterPreferredDistance = bPathTie
			&& PreferredDistanceError < BestPreferredDistanceError - KINDA_SMALL_NUMBER;
		const bool bPreferredTie = bPathTie
			&& FMath::IsNearlyEqual(PreferredDistanceError, BestPreferredDistanceError);
		const bool bBetterTargetDistance = bPreferredTie
			&& TargetDistanceSquared < BestTargetDistanceSquared - 1.0f;
		const bool bDeterministicIdTie = bPreferredTie
			&& FMath::IsNearlyEqual(TargetDistanceSquared, BestTargetDistanceSquared, 1.0f)
			&& (OutPointId == INDEX_NONE || CandidateId < OutPointId);
		if (bBetterPath || bBetterPreferredDistance || bBetterTargetDistance || bDeterministicIdTie)
		{
			BestPathTargetDistanceSquared = PathTargetDistanceSquared;
			BestPreferredDistanceError = PreferredDistanceError;
			BestTargetDistanceSquared = TargetDistanceSquared;
			OutPointId = CandidateId;
		}
	}

	return OutPointId != INDEX_NONE;
}

bool UBossDeckPointSelector::SelectWalkDestinationPoint(
	AEnemyShip& HostShip,
	AActor& BossActor,
	AActor& TargetActor,
	const FBossDestinationSelectionSettings& Settings,
	int32& OutPointId)
{
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(&BossActor);
	if (!Boss || !HostShip.GetDeckWaypoint(Boss->GetCurrentPointId()))
	{
		return false;
	}

	TArray<int32> CandidateIds;
	HostShip.GetConnectedDeckWaypointIds(Boss->GetCurrentPointId(), CandidateIds);
	CandidateIds.RemoveAll([&HostShip, &BossActor](const int32 PointId)
	{
		const UDeckWaypointComponent* Waypoint = HostShip.GetDeckWaypoint(PointId);
		return !Waypoint || !Waypoint->CanUseInCombat()
			|| !HostShip.IsDeckPointAvailable(PointId, &BossActor);
	});
	if (CandidateIds.Num() > 1)
	{
		CandidateIds.Remove(Boss->GetPreviousPointId());
	}

	const FVector DeckUp = HostShip.GetShipDeckMesh()
		? HostShip.GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	float BestRangeError = TNumericLimits<float>::Max();
	for (const int32 CandidateId : CandidateIds)
	{
		FVector CandidateLocation = HostShip.GetDeckWaypointWorldLocation(CandidateId);
		if (const ACharacter* BossCharacter = Cast<ACharacter>(&BossActor))
		{
			const UCapsuleComponent* Capsule = BossCharacter->GetCapsuleComponent();
			FTransform CharacterTransform;
			if (HostShip.ResolveDeckCharacterTransform(
				CandidateId,
				Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f,
				CharacterTransform))
			{
				CandidateLocation = CharacterTransform.GetLocation();
			}
		}

		if (Settings.bCheckDestinationOccupancy
			&& !IsDestinationClear(HostShip, BossActor, CandidateLocation, &TargetActor))
		{
			continue;
		}

		const float TargetDistance = FVector::VectorPlaneProject(
			CandidateLocation - TargetActor.GetActorLocation(), DeckUp).Size();
		const float RangeError = FMath::Abs(TargetDistance - FMath::Max(0.0f, Settings.IdealWalkRange));
		if (RangeError < BestRangeError - KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(RangeError, BestRangeError) && CandidateId < OutPointId))
		{
			BestRangeError = RangeError;
			OutPointId = CandidateId;
		}
	}

	return OutPointId != INDEX_NONE;
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

bool UBossDeckPointSelector::IsDestinationClear(
	const AEnemyShip& HostShip,
	const AActor& BossActor,
	const FVector& Destination,
	const AActor* TargetActor)
{
	const UWorld* World = HostShip.GetWorld();
	if (!World)
	{
		return false;
	}

	float Radius = 42.0f;
	float HalfHeight = 88.0f;
	if (const ACharacter* Character = Cast<ACharacter>(&BossActor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Radius = Capsule->GetScaledCapsuleRadius();
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BossDestinationClearance), false, &BossActor);
	QueryParams.AddIgnoredActor(&BossActor);
	// Do not ignore the target: teleporting into the player is an invalid destination.
	return !World->OverlapBlockingTestByChannel(
		Destination,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight),
		QueryParams);
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
	return !World->LineTraceSingleByChannel(Hit, Start, Destination, ECC_Visibility, QueryParams);
}
