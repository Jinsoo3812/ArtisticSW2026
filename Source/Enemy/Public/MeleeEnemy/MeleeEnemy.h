#pragma once

#include "CoreMinimal.h"
#include "BaseEnemy.h"

#include "MeleeEnemy.generated.h"

/**
 * Minimal ground melee enemy policy.
 *
 * Combat decisions remain in the shared Behavior Tree. This class only fixes
 * pawn rotation and default loadout behavior required by focus-driven strafing.
 */
UCLASS(Blueprintable)
class ENEMY_API AMeleeEnemy : public ABaseEnemy
{
	GENERATED_BODY()

public:
	AMeleeEnemy();
};
