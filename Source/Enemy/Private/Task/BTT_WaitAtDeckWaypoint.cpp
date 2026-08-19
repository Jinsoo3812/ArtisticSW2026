#include "Task/BTT_WaitAtDeckWaypoint.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "ShipAI/EnemyShip.h"

UBTT_WaitAtDeckWaypoint::UBTT_WaitAtDeckWaypoint()
{
	NodeName = TEXT("Wait At Deck Waypoint");
	bNotifyTick = true;
}

uint16 UBTT_WaitAtDeckWaypoint::GetInstanceMemorySize() const
{
	return sizeof(FWaitAtDeckWaypointMemory);
}

EBTNodeResult::Type UBTT_WaitAtDeckWaypoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ADeckRangedEnemy* Enemy = Controller ? Cast<ADeckRangedEnemy>(Controller->GetPawn()) : nullptr;
	AEnemyShip* HostShip = Enemy ? Cast<AEnemyShip>(Enemy->GetHostShip()) : nullptr;
	const UDeckWaypointComponent* Waypoint = HostShip && Enemy
		? HostShip->GetDeckWaypoint(Enemy->GetCurrentDeckWaypointId())
		: nullptr;
	if (!Enemy || !Enemy->IsPoolActive() || !Waypoint)
	{
		return EBTNodeResult::Failed;
	}

	FWaitAtDeckWaypointMemory* Memory = reinterpret_cast<FWaitAtDeckWaypointMemory*>(NodeMemory);
	Memory->RemainingTime = Waypoint->GetRandomWaitTime(Enemy->GetDeckRandomStream());
	return Memory->RemainingTime <= KINDA_SMALL_NUMBER
		? EBTNodeResult::Succeeded
		: EBTNodeResult::InProgress;
}

void UBTT_WaitAtDeckWaypoint::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	FWaitAtDeckWaypointMemory* Memory = reinterpret_cast<FWaitAtDeckWaypointMemory*>(NodeMemory);
	Memory->RemainingTime -= DeltaSeconds;
	if (Memory->RemainingTime <= 0.0f)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
