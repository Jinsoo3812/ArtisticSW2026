#include "Task/BTT_SummonDeckEnemy.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/BaseHealthComponent.h"

UBTT_SummonDeckEnemy::UBTT_SummonDeckEnemy()
{
	NodeName = TEXT("Summon One Deck Enemy");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTT_SummonDeckEnemy::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	return TryExecute(OwnerComp);
}

EBTNodeResult::Type UBTT_SummonDeckEnemy::TryExecute(UBehaviorTreeComponent& OwnerComp)
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	AShipBossEnemy* Boss = Controller ? Cast<AShipBossEnemy>(Controller->GetPawn()) : nullptr;
	if (!Boss || !Boss->HasAuthority() || Boss->GetHealthComponent()->IsDead()) return EBTNodeResult::Failed;
	const auto* ASC = Boss->GetAbilitySystemComponent();
	if (bWaitForBossIdle && (ASC->HasMatchingGameplayTag(State_Boss_Busy)
		|| ASC->HasMatchingGameplayTag(State_Status_Stun) || ASC->HasMatchingGameplayTag(State_Status_Knockback)))
		return EBTNodeResult::InProgress;
	ADeckEnemy* SummonedEnemy = nullptr;
	return Boss && Boss->TrySummonDeckEnemy(SummonedEnemy)
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}

void UBTT_SummonDeckEnemy::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	const auto Result = TryExecute(OwnerComp);
	if (Result != EBTNodeResult::InProgress) FinishLatentTask(OwnerComp, Result);
}
