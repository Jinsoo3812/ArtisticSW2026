#include "Decorator/BTD_DeckCombatCondition.h"
#include "AIController.h"
#include "AI/EnemyAlarmComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"

UBTD_DeckCombatCondition::UBTD_DeckCombatCondition()
{
	NodeName = TEXT("Deck Combat Condition");
	bCreateNodeInstance = true;
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTD_DeckCombatCondition, BlackboardKey), AActor::StaticClass());
}
bool UBTD_DeckCombatCondition::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* AI = OwnerComp.GetAIOwner();
	const ADeckEnemy* Enemy = AI ? Cast<ADeckEnemy>(AI->GetPawn()) : nullptr;
	AActor* Target = OwnerComp.GetBlackboardComponent() ? Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	const UDeckEnemyCombatComponent* Combat = Enemy ? Enemy->GetDeckCombatComponent() : nullptr;
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Combat) return false;
	if (Condition == EDeckCombatCondition::AttackReady && Combat->HasCommittedAttack()) return true;
	if (!Enemy->IsValidCombatTarget(Target)) return false;
	switch (Condition)
	{
	case EDeckCombatCondition::AttackReady: return Combat->EvaluateAttack(Target) == EDeckAttackOutcome::Ready;
	case EDeckCombatCondition::AttackCooldown: return Combat->IsCoolingDown();
	case EDeckCombatCondition::RecoveryPending: return Combat->HasRecovery(Target);
	case EDeckCombatCondition::AlarmPending:
		if (const UEnemyAlarmComponent* Alarm = Enemy->FindComponentByClass<UEnemyAlarmComponent>()) return Alarm->IsAlarmPending();
		return false;
	}
	return false;
}
void UBTD_DeckCombatCondition::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	Remaining = 0.0f; bLastResult = CalculateRawConditionValue(OwnerComp, NodeMemory);
}
void UBTD_DeckCombatCondition::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Remaining -= DeltaSeconds;
	if (Remaining > 0.0f) return;
	Remaining = FMath::Max(0.02f, CheckInterval);
	const bool bResult = CalculateRawConditionValue(OwnerComp, NodeMemory);
	if (bResult != bLastResult) { bLastResult = bResult; ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged); }
}
