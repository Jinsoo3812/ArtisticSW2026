#include "Task/BTT_GunnerBossCombat.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/BossGunCombatComponent.h"
#include "BossAI/BossGunMovementComponent.h"
#include "BossAI/GunnerBossTags.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BaseGameplayTags.h"

namespace
{
	AGunnerBossEnemy* Gunner(UBehaviorTreeComponent& Tree)
	{
		return Tree.GetAIOwner() ? Cast<AGunnerBossEnemy>(Tree.GetAIOwner()->GetPawn()) : nullptr;
	}
	bool ActivateTagged(UAbilitySystemComponent& ASC, FGameplayTag Tag)
	{
		FGameplayAbilitySpecHandle Handle;
		for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(Tag)) { Handle = Spec.Handle; break; }
		}
		return Handle.IsValid() && ASC.TryActivateAbility(Handle);
	}
}

UBTT_GunnerBossCombat::UBTT_GunnerBossCombat()
{
	NodeName = TEXT("Gunner Boss Combat");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTT_GunnerBossCombat::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	DecisionRemaining = 0.f;
	AGunnerBossEnemy* Boss = Gunner(OwnerComp);
	return Boss && Boss->HasAuthority() ? EBTNodeResult::InProgress : EBTNodeResult::Failed;
}

void UBTT_GunnerBossCombat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AGunnerBossEnemy* Boss = Gunner(OwnerComp);
	if (!Boss || !Boss->HasAuthority()) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	// A committed GA owns preparation, burst gaps and recovery, even if BT is aborted.
	if (Boss->GunCombat->IsAttackLocked() || ASC->HasMatchingGameplayTag(State_Boss_Busy)) return;
	AActor* Target = Boss->GetBossCombatTarget();
	if (!Boss->GunCombat->IsRuntimeValid(Target)) { Boss->GunMovement->Stop(); return; }
	const EBossGunMovementResult Position = Boss->GunMovement->UpdatePosition(Target, DeltaSeconds);
	DecisionRemaining -= DeltaSeconds;
	if (Position != EBossGunMovementResult::Ready || DecisionRemaining > 0.f) return;
	DecisionRemaining = FMath::Max(AttackDecisionInterval, KINDA_SMALL_NUMBER);
	if (!Boss->GunCombat->CanStart(Target)) return;
	ActivateTagged(*ASC, GunnerBossTags::Shot);
}

EBTNodeResult::Type UBTT_GunnerBossCombat::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AGunnerBossEnemy* Boss = Gunner(OwnerComp)) Boss->GunMovement->Stop();
	return EBTNodeResult::Aborted;
}

void UBTT_GunnerBossCombat::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	if (AGunnerBossEnemy* Boss = Gunner(OwnerComp)) Boss->GunMovement->Stop();
	Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
}
