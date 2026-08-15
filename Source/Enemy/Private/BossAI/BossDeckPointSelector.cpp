#include "BossAI/BossDeckPointSelector.h"

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
	const FBossDestinationSelectionSettings& Settings,
	int32& OutPointId)
{
	OutPointId = INDEX_NONE;
	if (!IsValid(HostShip) || !IsValid(BossActor) || !IsValid(TargetActor))
	{
		return false;
	}

	const FVector DeckUp = HostShip->GetShipDeckMesh()
		? HostShip->GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	const FVector BossLocation = BossActor->GetActorLocation();
	const FVector TargetLocation = TargetActor->GetActorLocation();

	TArray<int32> CandidateIds;
	HostShip->GetDeckWaypointIds(CandidateIds, true);

	float BestTargetDistanceSquared = TNumericLimits<float>::Max();
	float BestBossDistanceSquared = TNumericLimits<float>::Max();
	for (const int32 CandidateId : CandidateIds)
	{
		const UDeckWaypointComponent* Waypoint = HostShip->GetDeckWaypoint(CandidateId);
		if (!IsValid(Waypoint))
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
		if (BossToCandidateOnDeck.SizeSquared()
			< FMath::Square(FMath::Max(0.0f, Settings.MinimumTravelDistance)))
		{
			continue;
		}

		if (!IsPointBehindTarget(
			TargetLocation,
			TargetActor->GetActorForwardVector(),
			CandidateLocation,
			DeckUp,
			Settings.MaximumRearDot))
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

		const float TargetDistanceSquared = FVector::DistSquared(TargetLocation, CandidateLocation);
		const float BossDistanceSquared = FVector::DistSquared(BossLocation, CandidateLocation);
		const bool bCloserToTarget = TargetDistanceSquared < BestTargetDistanceSquared - 1.0f;
		const bool bTargetTieAndCloserToBoss =
			FMath::IsNearlyEqual(TargetDistanceSquared, BestTargetDistanceSquared, 1.0f)
			&& BossDistanceSquared < BestBossDistanceSquared;
		if (bCloserToTarget || bTargetTieAndCloserToBoss)
		{
			BestTargetDistanceSquared = TargetDistanceSquared;
			BestBossDistanceSquared = BossDistanceSquared;
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
	if (FVector::DistSquared(Start, Destination)
		> FMath::Square(FMath::Max(1.0f, Settings.MaximumDashDistance)))
	{
		return false;
	}

	if (!DoesSegmentPassTarget(Start, Destination, TargetActor.GetActorLocation(), Settings.DashHitCorridorRadius))
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
