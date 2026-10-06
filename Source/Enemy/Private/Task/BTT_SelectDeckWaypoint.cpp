#include "Task/BTT_SelectDeckWaypoint.h"

#include "AIController.h"
#include "AI/BaseAIController.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "AI/PointSelectionFailure.h"
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
	bNotifyTick = true;
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
	const auto FailPointSelection = [this, Enemy, Navigation, Route](const TCHAR* Reason)
	{
		EnemyPointSelectionFailure::Log(this, Enemy, Reason);
		if (Navigation) Navigation->CancelCombatRoute();
		if (Route) Route->ClearGoal();
		return EBTNodeResult::Failed;
	};
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Area || !Area->IsReady() || !Navigation || !Route) return EBTNodeResult::Failed;
	if (SelectionMode == EDeckWaypointSelectionMode::Patrol)
	{
		Navigation->CancelCombatRoute();
		if (!Route->SetPatrolGoal(Enemy->GetDeckRandomStream())) return FailPointSelection(TEXT("No reachable deck patrol point."));
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
			if (!Blackboard || !Blackboard->IsVectorValueSet(TEXT("PointOfInterest"))) return FailPointSelection(TEXT("No investigation point."));
			Point = Blackboard->GetValueAsVector(TEXT("PointOfInterest"));
		}
		return Navigation->PlanInvestigationRoute(Point) ? EBTNodeResult::Succeeded : FailPointSelection(TEXT("No reachable investigation point."));
	}
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Enemy->IsValidCombatTarget(Target))
	{
		Navigation->CancelCombatRoute();
		if (auto* AI = Cast<ABaseAIController>(Controller)) AI->ClearCombatTarget(true);
		return EBTNodeResult::Failed;
	}
	const auto HoldOrKeepRoute = [&]()
	{
		if (Navigation->HasActiveRoute() && Route->HasGoal() && Area->IsLocationValid(Route->GetGoal()))
			return EBTNodeResult::Succeeded;
		*reinterpret_cast<float*>(NodeMemory) = 0.f;
		return EBTNodeResult::InProgress;
	};
	FDeckTargetAnchor TargetFloor;
	if (!UDeckCombatTargetResolverComponent::ResolveFor(Enemy, Target, TargetFloor))
	{
		const auto* Resolver = Enemy->FindComponentByClass<UDeckCombatTargetResolverComponent>();
		if (Resolver && Resolver->HasExpiredEvidence(Target))
		{
			if (auto* AI = Cast<ABaseAIController>(Controller))
			{
				const bool bHaveSnapshot = Navigation->HasActiveRoute() && Route->HasGoal() && Area->IsLocationValid(Route->GetGoal());
				const FVector Point = bHaveSnapshot ? Area->ToWorld(Route->GetGoal().LocalFloor) : Enemy->GetActorLocation();
				AI->ClearCombatTarget(true);
				Navigation->CancelCombatRoute();
				if (bHaveSnapshot) AI->StartInvestigation(Point);
			}
			return EBTNodeResult::Failed;
		}
		return HoldOrKeepRoute();
	}
	if (SelectionMode == EDeckWaypointSelectionMode::Combat)
	{
		UDeckEnemyCombatComponent* Combat = Enemy->GetDeckCombatComponent();
		if (Combat->EvaluateAttack(Target, false) == EDeckAttackOutcome::BlockedLOS)
		{
			if (!Combat->HasStoredRecovery()) Combat->RecordBlockedLOS(Combat->BeginAttack(Target), Target);
			return Navigation->PlanRecoveryRoute(Target) ? EBTNodeResult::Succeeded : HoldOrKeepRoute();
		}
	}
	const bool bSelected = SelectionMode == EDeckWaypointSelectionMode::ReleaseLineOfSightReposition
		? Navigation->PlanRecoveryRoute(Target) : Navigation->PlanTargetDistanceRoute(Target, TargetDistance, ProjectionTolerance);
	return bSelected ? EBTNodeResult::Succeeded : HoldOrKeepRoute();
}
void UBTT_SelectDeckWaypoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	float& Waiting = *reinterpret_cast<float*>(NodeMemory); Waiting += DeltaSeconds;
	// A bounded observation avoids an immediate failure loop without removing the live target.
	if (Waiting >= 0.3f) FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
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
