#include "Task/BTT_SelectSamuraiDashPoint.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "BossAI/SamuraiDashRecoveryComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "ShipAI/EnemyShip.h"

namespace { struct FSamuraiSelectionMemory { float Elapsed = 0.f; float SinceAttempt = 0.f; uint32 TargetId = 0; }; }

UBTT_SelectSamuraiDashPoint::UBTT_SelectSamuraiDashPoint()
{
	NodeName = TEXT("Select Samurai Dash Point (Bounded Retry)");
	SelectionPurpose = EBossDestinationPurpose::Dash;
	bNotifyTaskFinished = true;
}

uint16 UBTT_SelectSamuraiDashPoint::GetInstanceMemorySize() const { return sizeof(FSamuraiSelectionMemory); }

EBTNodeResult::Type UBTT_SelectSamuraiDashPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	auto& Memory = *reinterpret_cast<FSamuraiSelectionMemory*>(NodeMemory);
	Memory = FSamuraiSelectionMemory();
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	if (Boss && Boss->GetBossCombatTarget()) Memory.TargetId = Boss->GetBossCombatTarget()->GetUniqueID();
	return TrySelect(OwnerComp);
}

EBTNodeResult::Type UBTT_SelectSamuraiDashPoint::TrySelect(UBehaviorTreeComponent& OwnerComp)
{
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	auto* BB = OwnerComp.GetBlackboardComponent();
	auto* Policy = Boss ? Boss->GetSamuraiDashRecoveryComponent() : nullptr;
	AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(TargetActorKey.SelectedKeyName)) : nullptr;
	if (!Target && Boss) Target = Boss->GetBossCombatTarget();
	if (!Boss || !BB || !Policy || SelectionPurpose != EBossDestinationPurpose::Dash
		|| !Policy->RefreshContext(Target) || !Policy->CanTryDash()
		|| Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy)) return EBTNodeResult::Failed;
	const auto* Area = Boss->GetHostShip() ? Boss->GetHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start;
	FDeckTargetAnchor Anchor;
	if (!Area || !Area->IsReady() || !Area->ResolveActorOnDeck(*Boss, Start)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(Boss, Target, Anchor)) return EBTNodeResult::Failed;
	FDeckWalkLocation Goal;
	if (UBossDeckPointSelector::SelectDestinationLocation(Boss->GetHostShip(), Boss, Target,
		EBossDestinationPurpose::Dash, DestinationRelation, SelectionSettings, Goal) && Boss->TrySetDestinationLocation(Goal, false))
	{
		BB->SetValueAsVector(GetSelectedBlackboardKey(), Goal.LocalFloor);
		// Reset only after the ability succeeds; activation-time point rejection must accumulate too.
		return EBTNodeResult::Succeeded;
	}
	if (!Policy->RecordPointFailure(Target, TEXT("No suitable preselected Dash destination/path."))) return EBTNodeResult::Failed;
	return Policy->NeedsReposition() ? EBTNodeResult::Failed : EBTNodeResult::InProgress;
}

void UBTT_SelectSamuraiDashPoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	auto& Memory = *reinterpret_cast<FSamuraiSelectionMemory*>(NodeMemory);
	Memory.Elapsed += DeltaSeconds;
	Memory.SinceAttempt += DeltaSeconds;
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	auto* Policy = Boss ? Boss->GetSamuraiDashRecoveryComponent() : nullptr;
	AActor* Target = Boss ? Boss->GetBossCombatTarget() : nullptr;
	if (!Policy || !Target || Target->GetUniqueID() != Memory.TargetId
		|| Memory.Elapsed >= Policy->GetMaximumSelectionTime())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	if (Memory.SinceAttempt < Policy->GetSelectionRetryInterval()) return;
	Memory.SinceAttempt = 0.f;
	const auto Result = TrySelect(OwnerComp);
	if (Result != EBTNodeResult::InProgress) FinishLatentTask(OwnerComp, Result);
}

void UBTT_SelectSamuraiDashPoint::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	AShipBossEnemy* Boss = OwnerComp.GetAIOwner() ? Cast<AShipBossEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	if (Result != EBTNodeResult::Succeeded && Boss
		&& !Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy))
	{
		Boss->ClearDestination();
		if (auto* BB = OwnerComp.GetBlackboardComponent()) BB->ClearValue(GetSelectedBlackboardKey());
		OwnerComp.GetAIOwner()->ClearFocus(EAIFocusPriority::Gameplay);
	}
	Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
}

FString UBTT_SelectSamuraiDashPoint::GetStaticDescription() const
{
	return TEXT("Samurai Combat subtree only. Shared Dash failures -> bounded retry -> priority Walk reposition.");
}
