#include "ShipAI/BTT_EnableEnemyShipNavigation.h"

#include "AIController.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UBTT_EnableEnemyShipNavigation::UBTT_EnableEnemyShipNavigation()
{
	NodeName = TEXT("Set Enemy Ship Navigation Enabled");
}

EBTNodeResult::Type UBTT_EnableEnemyShipNavigation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	AEnemyShip* Ship = Controller ? Cast<AEnemyShip>(Controller->GetPawn()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Navigation)
	{
		return EBTNodeResult::Failed;
	}

	Navigation->SetNavigationEnabled(bEnableNavigation);
	return EBTNodeResult::Succeeded;
}
