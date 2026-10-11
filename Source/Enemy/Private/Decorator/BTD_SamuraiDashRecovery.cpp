#include "Decorator/BTD_SamuraiDashRecovery.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "BossAI/SamuraiDashRecoveryComponent.h"

UBTD_SamuraiDashRecovery::UBTD_SamuraiDashRecovery()
{
	NodeName = TEXT("Samurai Dash Recovery Condition");
	FlowAbortMode = EBTFlowAbortMode::None;
	bTickIntervals = true;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
}

void UBTD_SamuraiDashRecovery::Configure(ESamuraiDashRecoveryCondition InCondition)
{
	Condition = InCondition;
	FlowAbortMode = Condition == ESamuraiDashRecoveryCondition::RepositionReady
		? EBTFlowAbortMode::LowerPriority : EBTFlowAbortMode::None;
}

bool UBTD_SamuraiDashRecovery::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	auto* Policy = Boss ? Boss->GetSamuraiDashRecoveryComponent() : nullptr;
	if (!Policy || !Policy->IsEnabled()) return Condition != ESamuraiDashRecoveryCondition::RepositionReady;
	if (!Policy->RefreshContext(Boss->GetBossCombatTarget())) return false;
	switch (Condition)
	{
	case ESamuraiDashRecoveryCondition::RepositionReady: return Policy->CanStartReposition();
	case ESamuraiDashRecoveryCondition::AllowDash: return Policy->CanTryDash();
	default: return !Policy->NeedsReposition();
	}
}

void UBTD_SamuraiDashRecovery::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	SetNextTickTime(NodeMemory, 0.1f);
}

void UBTD_SamuraiDashRecovery::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (Condition == ESamuraiDashRecoveryCondition::RepositionReady)
		ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	SetNextTickTime(NodeMemory, 0.1f);
}

FString UBTD_SamuraiDashRecovery::GetStaticDescription() const
{
	return Condition == ESamuraiDashRecoveryCondition::RepositionReady ? TEXT("Pending Dash reposition; no Busy/hidden/deck recovery")
		: (Condition == ESamuraiDashRecoveryCondition::AllowDash ? TEXT("No Dash reposition or failed-move retry delay")
		: TEXT("Yield all normal actions to pending Dash reposition"));
}
