#include "Task/BTT_DeckAttack.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTT_DeckAttack::UBTT_DeckAttack()
{
	NodeName = TEXT("Deck Weapon Attack"); bCreateNodeInstance = true; bNotifyTick = bNotifyTaskFinished = true;
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_DeckAttack, BlackboardKey), AActor::StaticClass());
}
EBTNodeResult::Type UBTT_DeckAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Cleanup(); bEnding = bEndedSynchronously = false; Elapsed = 0.0f;
	AAIController* AI = OwnerComp.GetAIOwner();
	ADeckEnemy* Enemy = AI ? Cast<ADeckEnemy>(AI->GetPawn()) : nullptr;
	AActor* Target = OwnerComp.GetBlackboardComponent() ? Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	Combat = Enemy ? Enemy->GetDeckCombatComponent() : nullptr;
	ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	if (!Combat.IsValid() || !ASC.IsValid() || Combat->EvaluateAttack(Target) != EDeckAttackOutcome::Ready
		|| !Combat->FindAttackAbility(AbilityHandle)) { Cleanup(); return EBTNodeResult::Failed; }
	Enemy->SetCombatTarget(Target);
	Enemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
	AI->StopMovement(); Enemy->GetCharacterMovement()->StopMovementImmediately();
	Behavior = &OwnerComp;
	EndHandle = ASC->OnAbilityEnded.AddUObject(this, &UBTT_DeckAttack::OnAbilityEnded);
	bActivating = true;
	const bool bStarted = Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Ranged
		? Enemy->TryStartRangedAttack(AbilityHandle) : ASC->TryActivateAbility(AbilityHandle, false);
	bActivating = false;
	Attempt = Combat->GetAttemptId();
	if (!bStarted || bEndedSynchronously)
	{
		const EBTNodeResult::Type Result = bStarted ? GetResult() : EBTNodeResult::Failed;
		Cleanup(); return Result;
	}
	return EBTNodeResult::InProgress;
}
EBTNodeResult::Type UBTT_DeckAttack::GetResult() const
{
	return Combat.IsValid() && Combat->GetAttemptId() == Attempt && Combat->GetLastOutcome() == EDeckAttackOutcome::Executed
		? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
void UBTT_DeckAttack::OnAbilityEnded(const FAbilityEndedData& Data)
{
	if (bEnding || Data.AbilitySpecHandle != AbilityHandle) return;
	if (bActivating) { bEndedSynchronously = true; return; }
	bEnding = true;
	const EBTNodeResult::Type Result = GetResult();
	UBehaviorTreeComponent* OwnerComp = Behavior.Get(); Cleanup();
	if (OwnerComp) FinishLatentTask(*OwnerComp, Result);
}
void UBTT_DeckAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	if (Combat.IsValid() && Combat->IsCurrentAttack(Attempt)
		&& (Elapsed < MaximumAttackTime || Combat->IsAttackMontagePlaying())) return;
	bEnding = true; CancelAttack();
	const EBTNodeResult::Type Result = GetResult(); Cleanup(); FinishLatentTask(OwnerComp, Result);
}
void UBTT_DeckAttack::CancelAttack()
{
	if (ASC.IsValid() && EndHandle.IsValid()) ASC->OnAbilityEnded.Remove(EndHandle);
	EndHandle.Reset();
	if (ASC.IsValid() && AbilityHandle.IsValid()) ASC->CancelAbilityHandle(AbilityHandle);
}
EBTNodeResult::Type UBTT_DeckAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bEnding = true; CancelAttack(); Cleanup(); return EBTNodeResult::Aborted;
}
void UBTT_DeckAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	Cleanup(); Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
}
void UBTT_DeckAttack::Cleanup()
{
	if (ASC.IsValid() && EndHandle.IsValid()) ASC->OnAbilityEnded.Remove(EndHandle);
	EndHandle.Reset(); AbilityHandle = FGameplayAbilitySpecHandle(); Behavior.Reset(); ASC.Reset(); Combat.Reset(); Attempt = 0;
}
