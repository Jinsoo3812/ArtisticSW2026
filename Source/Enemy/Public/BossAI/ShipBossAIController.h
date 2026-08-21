#pragma once

#include "CoreMinimal.h"
#include "AI/BaseAIController.h"
#include "ShipBossAIController.generated.h"

/** Strict BT-only controller for the ship boss. */
UCLASS()
class ENEMY_API AShipBossAIController : public ABaseAIController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;
};
