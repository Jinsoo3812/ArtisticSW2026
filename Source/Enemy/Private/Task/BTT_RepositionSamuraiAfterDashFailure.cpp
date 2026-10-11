#include "Task/BTT_RepositionSamuraiAfterDashFailure.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "BossAI/SamuraiDashRecoveryComponent.h"

UBTT_RepositionSamuraiAfterDashFailure::UBTT_RepositionSamuraiAfterDashFailure()
{
	NodeName = TEXT("Reposition Samurai After Dash Failure");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
	bStopWhenAttackReady = false;
	MaximumMoveTime = 3.f;
	WalkSelectionSettings.MinimumWalkTravelDistance = 200.f;
}

EBTNodeResult::Type UBTT_RepositionSamuraiAfterDashFailure::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bStartedReposition = false;
	RepositionTarget.Reset();
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	auto* Policy = Boss ? Boss->GetSamuraiDashRecoveryComponent() : nullptr;
	AActor* Target = Boss ? Boss->GetBossCombatTarget() : nullptr;
	if (!Policy || !Policy->RefreshContext(Target) || !Policy->BeginReposition()) return EBTNodeResult::Failed;
	bStartedReposition = true;
	RepositionTarget = Target;
	Boss->ClearDestination();
	if (auto* BB = OwnerComp.GetBlackboardComponent()) BB->ClearValue(TEXT("DestinationLocation"));
	OwnerComp.GetAIOwner()->SetFocus(Target, EAIFocusPriority::Gameplay);
	FDeckWalkLocation Goal;
	if (!UBossDeckPointSelector::SelectDestinationLocation(Boss->GetHostShip(), Boss, Target,
		EBossDestinationPurpose::Walk, EBossDestinationRelation::Any, WalkSelectionSettings, Goal)
		|| !Boss->TrySetDestinationLocation(Goal, true)) return EBTNodeResult::Failed;
	// Keep the chosen relocation fixed. Generic combat movement may chase a moving target;
	// this bounded reposition deliberately completes a meaningful change of position first.
	if (auto* BB = OwnerComp.GetBlackboardComponent()) BB->SetValueAsVector(TEXT("DestinationLocation"), Goal.LocalFloor);
	return Super::ExecuteTask(OwnerComp, NodeMemory);
}

void UBTT_RepositionSamuraiAfterDashFailure::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	auto* Policy = Boss ? Boss->GetSamuraiDashRecoveryComponent() : nullptr;
	if (!Policy || Boss->GetBossCombatTarget() != RepositionTarget.Get()
		|| !Policy->RefreshContext(RepositionTarget.Get()) || !Policy->NeedsReposition())
	{
		Super::AbortTask(OwnerComp, NodeMemory);
		FinishLatentTask(OwnerComp, EBTNodeResult::Aborted);
		return;
	}
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);
}

void UBTT_RepositionSamuraiAfterDashFailure::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	if (bStartedReposition && Boss)
	{
		if (auto* Policy = Boss->GetSamuraiDashRecoveryComponent())
			Policy->FinishReposition(Result == EBTNodeResult::Succeeded, Result == EBTNodeResult::Aborted);
		// Also covers failure during Walk selection, before the shared movement task started.
		if (!Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy))
		{
			Boss->ClearDestination();
			if (auto* BB = OwnerComp.GetBlackboardComponent()) BB->ClearValue(TEXT("DestinationLocation"));
			if (OwnerComp.GetAIOwner()) OwnerComp.GetAIOwner()->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
	bStartedReposition = false;
	RepositionTarget.Reset();
	Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
}

FString UBTT_RepositionSamuraiAfterDashFailure::GetStaticDescription() const
{
	return FString::Printf(TEXT("Samurai only. Fresh Walk goal >= %.0fcm; max %.1fs; clear policy/focus on finish."),
		WalkSelectionSettings.MinimumWalkTravelDistance, MaximumMoveTime);
}
