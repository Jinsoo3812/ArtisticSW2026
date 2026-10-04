#include "Task/BTT_SelectDeckWaypoint.h"

#include "AIController.h"
#include "AI/EnemyAlarmComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "ShipAI/EnemyShip.h"

UBTT_SelectDeckWaypoint::UBTT_SelectDeckWaypoint()
{
	NodeName = TEXT("Select Deck Walk Goal");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_SelectDeckWaypoint, BlackboardKey), AActor::StaticClass());
}
EBTNodeResult::Type UBTT_SelectDeckWaypoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ADeckEnemy* Enemy = Controller ? Cast<ADeckEnemy>(Controller->GetPawn()) : nullptr;
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckEnemyNavigationComponent* Navigation = Enemy ? Enemy->GetDeckEnemyNavigationComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->GetDeckWalkRouteComponent() : nullptr;
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Area || !Area->IsReady() || !Navigation || !Route) return EBTNodeResult::Failed;
	if (SelectionMode == EDeckWaypointSelectionMode::Patrol)
	{
		Navigation->CancelCombatRoute();
		if (!Route->SetPatrolGoal(Enemy->GetDeckRandomStream())) return EBTNodeResult::Failed;
		Enemy->BeginFreeDeckMovement();
		return EBTNodeResult::Succeeded;
	}
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (SelectionMode == EDeckWaypointSelectionMode::Investigation)
	{
		FVector Point;
		const UEnemyAlarmComponent* Alarm = Enemy->FindComponentByClass<UEnemyAlarmComponent>();
		if (!Alarm || !Alarm->GetInvestigationWorld(Point))
		{
			if (!Blackboard || !Blackboard->IsVectorValueSet(TEXT("PointOfInterest"))) return EBTNodeResult::Failed;
			Point = Blackboard->GetValueAsVector(TEXT("PointOfInterest"));
		}
		return Navigation->PlanInvestigationRoute(Point) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
	}
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	FDeckWalkLocation TargetFloor;
	if (!Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Target, TargetFloor))
	{
		Navigation->CancelCombatRoute();
		Enemy->ClearCombatTarget();
		return EBTNodeResult::Failed;
	}
	if (SelectionMode == EDeckWaypointSelectionMode::Combat)
	{
		UDeckEnemyCombatComponent* Combat = Enemy->GetDeckCombatComponent();
		if (Combat->EvaluateAttack(Target, false) == EDeckAttackOutcome::BlockedLOS)
		{
			if (!Combat->HasStoredRecovery()) Combat->RecordBlockedLOS(Combat->BeginAttack(Target), Target);
			return Navigation->PlanRecoveryRoute(Target) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
		}
	}
	const bool bSelected = SelectionMode == EDeckWaypointSelectionMode::ReleaseLineOfSightReposition
		? Navigation->PlanRecoveryRoute(Target) : Navigation->PlanTargetDistanceRoute(Target, TargetDistance, ProjectionTolerance);
	return bSelected ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
FString UBTT_SelectDeckWaypoint::GetStaticDescription() const
{
	switch (SelectionMode)
	{
	case EDeckWaypointSelectionMode::Combat: return FString::Printf(TEXT("Player-to-enemy line: %.0f cm from Player; projection tolerance %.0f cm"), TargetDistance, ProjectionTolerance);
	case EDeckWaypointSelectionMode::ReleaseLineOfSightReposition: return TEXT("Choose a reachable deck location after blocked release LOS");
	case EDeckWaypointSelectionMode::Investigation: return TEXT("Investigate a fixed alarm location on the moving deck without acquiring a combat target");
	default: return TEXT("Choose a reachable deck patrol location");
	}
}
