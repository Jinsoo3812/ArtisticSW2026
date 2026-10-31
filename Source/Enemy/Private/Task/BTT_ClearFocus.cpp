#include "Task/BTT_ClearFocus.h"

#include "AIController.h"
#include "AI/BaseAIController.h"

UBTT_ClearFocus::UBTT_ClearFocus()
{
	NodeName = TEXT("Clear Focus");
}

EBTNodeResult::Type UBTT_ClearFocus::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	// A combat sequence finishing is not the same as losing its deck player.
	if (auto* DeckAI = Cast<ABaseAIController>(AIController); DeckAI && DeckAI->RefreshDeckCombatFocus())
		return EBTNodeResult::Succeeded;
	AIController->ClearFocus(EAIFocusPriority::Gameplay);
	return EBTNodeResult::Succeeded;
}
