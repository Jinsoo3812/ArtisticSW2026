#include "ShipAI/BTTask_NavalDrive.h"

#include "AIController.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UBTTask_NavalDrive::UBTTask_NavalDrive()
{
	NodeName = TEXT("[LEGACY] Enable Enemy Ship Navigation Compatibility");
}

EBTNodeResult::Type UBTTask_NavalDrive::ExecuteTask(
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

	Navigation->SetNavigationEnabled(true);
	return EBTNodeResult::Succeeded;
}
