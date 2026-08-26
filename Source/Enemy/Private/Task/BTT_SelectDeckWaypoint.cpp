#include "Task/BTT_SelectDeckWaypoint.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "ShipAI/EnemyShip.h"

UBTT_SelectDeckWaypoint::UBTT_SelectDeckWaypoint()
{
	NodeName = TEXT("Select Deck Waypoint");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_SelectDeckWaypoint, BlackboardKey),
		AActor::StaticClass());
}

EBTNodeResult::Type UBTT_SelectDeckWaypoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ADeckRangedEnemy* Enemy = Controller ? Cast<ADeckRangedEnemy>(Controller->GetPawn()) : nullptr;
	AEnemyShip* HostShip = Enemy ? Cast<AEnemyShip>(Enemy->GetHostShip()) : nullptr;
	if (!Enemy || !Enemy->IsPoolActive() || !HostShip)
	{
		return EBTNodeResult::Failed;
	}

	int32 CurrentId = Enemy->GetCurrentDeckWaypointId();
	if (!HostShip->GetDeckWaypoint(CurrentId))
	{
		CurrentId = HostShip->FindNearestDeckWaypoint(Enemy->GetActorLocation());
	}

	TArray<int32> LinkedIds;
	HostShip->GetConnectedDeckWaypointIds(CurrentId, LinkedIds);
	if (LinkedIds.IsEmpty())
	{
		return EBTNodeResult::Failed;
	}

	AActor* TargetActor = nullptr;
	if (SelectionMode == EDeckWaypointSelectionMode::Combat)
	{
		if (UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent())
		{
			TargetActor = Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()));
		}
		if (!Enemy->IsValidCombatTarget(TargetActor))
		{
			TargetActor = nullptr;
		}
	}

	TArray<int32> Candidates;
	for (const int32 LinkedId : LinkedIds)
	{
		const UDeckWaypointComponent* Waypoint = HostShip->GetDeckWaypoint(LinkedId);
		const bool bAllowed = Waypoint && (TargetActor
			? Waypoint->CanUseInCombat()
			: Waypoint->CanPatrol())
			&& HostShip->IsDeckPointAvailable(LinkedId, Enemy);
		if (bAllowed)
		{
			Candidates.AddUnique(LinkedId);
		}
	}

	if (Candidates.IsEmpty())
	{
		return EBTNodeResult::Failed;
	}

	int32 SelectedId = INDEX_NONE;
	if (!TargetActor)
	{
		if (Candidates.Num() > 1)
		{
			Candidates.Remove(Enemy->GetPreviousDeckWaypointId());
		}
		SelectedId = Candidates[Enemy->GetDeckRandomStream().RandRange(0, Candidates.Num() - 1)];
	}
	else
	{
		const float PreferredRange = (Enemy->GetMinAttackRange() + Enemy->GetMaxAttackRange()) * 0.5f;
		float BestScore = -TNumericLimits<float>::Max();
		for (const int32 CandidateId : Candidates)
		{
			const FVector CandidateLocation = HostShip->GetDeckWaypointWorldLocation(CandidateId);
			const float RangeScore = -FMath::Abs(
				FVector::Dist(CandidateLocation, TargetActor->GetActorLocation()) - PreferredRange);

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeckWaypointCombatLOS), true, Enemy);
			FHitResult Hit;
			const bool bBlocked = HostShip->GetWorld()->LineTraceSingleByChannel(
				Hit,
				CandidateLocation + FVector::UpVector * 100.0f,
				TargetActor->GetActorLocation() + FVector::UpVector * 60.0f,
				ECC_Visibility,
				QueryParams)
				&& Hit.GetActor() != TargetActor;
			const float Score = RangeScore + (bBlocked ? 0.0f : 5000.0f);
			if (Score > BestScore)
			{
				BestScore = Score;
				SelectedId = CandidateId;
			}
		}
	}

	if (SelectedId == INDEX_NONE)
	{
		return EBTNodeResult::Failed;
	}

	return Enemy->TrySetGoalDeckWaypointId(SelectedId)
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}

FString UBTT_SelectDeckWaypoint::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Choose one linked moving-deck point (%s)"),
		SelectionMode == EDeckWaypointSelectionMode::Combat ? TEXT("Combat") : TEXT("Patrol"));
}
