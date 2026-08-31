#include "Task/BTT_SummonDeckEnemy.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BossAI/ShipBossEnemy.h"

UBTT_SummonDeckEnemy::UBTT_SummonDeckEnemy()
{
	NodeName = TEXT("Summon One Deck Enemy");
}

EBTNodeResult::Type UBTT_SummonDeckEnemy::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	AShipBossEnemy* Boss = Controller ? Cast<AShipBossEnemy>(Controller->GetPawn()) : nullptr;
	ADeckEnemy* SummonedEnemy = nullptr;
	return Boss && Boss->TrySummonDeckEnemy(SummonedEnemy)
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}
