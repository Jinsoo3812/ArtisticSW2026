#include "Task/BTT_WaitAtDeckWaypoint.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "DeckAI/DeckRangedEnemy.h"

UBTT_WaitAtDeckWaypoint::UBTT_WaitAtDeckWaypoint()
{
	NodeName = TEXT("Wait After Deck Patrol");
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
	ADeckEnemy* Enemy = Controller ? Cast<ADeckEnemy>(Controller->GetPawn()) : nullptr;
	if (!Enemy || !Enemy->CanMoveOnDeck())
	{
		return EBTNodeResult::Failed;
	}

	FWaitAtDeckWaypointMemory* Memory = reinterpret_cast<FWaitAtDeckWaypointMemory*>(NodeMemory);
	const float Minimum = FMath::Max(0.0f, MinWaitTime);
	Memory->RemainingTime = Enemy->GetDeckRandomStream().FRandRange(Minimum, FMath::Max(Minimum, MaxWaitTime));
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
