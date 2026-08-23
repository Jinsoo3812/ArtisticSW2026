#include "Decorator/BTD_CanActivateAbilityByTag.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTD_CanActivateAbilityByTag::UBTD_CanActivateAbilityByTag()
{
	NodeName = TEXT("Can Activate Ability By Tag");
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
	bTickIntervals = true;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
}

bool UBTD_CanActivateAbilityByTag::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const ABaseEnemy* Enemy = Controller ? Cast<ABaseEnemy>(Controller->GetPawn()) : nullptr;
	UAbilitySystemComponent* ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	const FGameplayAbilityActorInfo* ActorInfo = ASC ? ASC->AbilityActorInfo.Get() : nullptr;
	if (!ASC || !ActorInfo || !AbilityAssetTag.IsValid())
	{
		return false;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability
			&& Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag)
			&& Spec.Ability->CanActivateAbility(Spec.Handle, ActorInfo))
		{
			return true;
		}
	}
	return false;
}

void UBTD_CanActivateAbilityByTag::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	SetNextTickTime(NodeMemory, FMath::Max(0.02f, EvaluationInterval));
}

void UBTD_CanActivateAbilityByTag::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	SetNextTickTime(NodeMemory, FMath::Max(0.02f, EvaluationInterval));
}

FString UBTD_CanActivateAbilityByTag::GetStaticDescription() const
{
	return FString::Printf(TEXT("Can activate %s"), *AbilityAssetTag.ToString());
}
