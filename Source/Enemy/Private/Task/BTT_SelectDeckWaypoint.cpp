#include "Task/BTT_SelectDeckWaypoint.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
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
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	FDeckWalkLocation TargetFloor;
	if (!Enemy->IsValidCombatTarget(Target) || !Area->ResolveActorOnDeck(*Target, TargetFloor))
	{
		Navigation->CancelCombatRoute();
		Enemy->ClearCombatTarget();
		return EBTNodeResult::Failed;
	}
	const bool bSelected = SelectionMode == EDeckWaypointSelectionMode::ReleaseLineOfSightReposition
		? Navigation->PrepareReleaseLineOfSightReposition(Target) : Navigation->PlanCombatRoute(Target, true);
	return bSelected ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
FString UBTT_SelectDeckWaypoint::GetStaticDescription() const
{
	switch (SelectionMode)
	{
	case EDeckWaypointSelectionMode::Combat: return TEXT("Choose a reachable combat location on the deck walk area");
	case EDeckWaypointSelectionMode::ReleaseLineOfSightReposition: return TEXT("Choose a reachable deck location after blocked release LOS");
	default: return TEXT("Choose a reachable deck patrol location");
	}
}
