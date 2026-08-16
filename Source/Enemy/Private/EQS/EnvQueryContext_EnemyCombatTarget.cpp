#include "EQS/EnvQueryContext_EnemyCombatTarget.h"

#include "AI/BaseAIController.h"
#include "GameFramework/Pawn.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"

void UEnvQueryContext_EnemyCombatTarget::ProvideContext(
	FEnvQueryInstance& QueryInstance,
	FEnvQueryContextData& ContextData) const
{
	if (AActor* CombatTarget = ResolveCombatTarget(QueryInstance.Owner.Get()))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, CombatTarget);
	}
}

AActor* UEnvQueryContext_EnemyCombatTarget::ResolveCombatTarget(const UObject* QueryOwner)
{
	const ABaseAIController* AIController = Cast<ABaseAIController>(QueryOwner);
	if (!AIController)
	{
		const APawn* PawnOwner = Cast<APawn>(QueryOwner);
		AIController = PawnOwner ? Cast<ABaseAIController>(PawnOwner->GetController()) : nullptr;
	}

	AActor* CombatTarget = AIController ? AIController->GetCombatTarget() : nullptr;
	return IsValid(CombatTarget) ? CombatTarget : nullptr;
}
