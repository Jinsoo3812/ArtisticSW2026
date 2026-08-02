#pragma once

#include "CoreMinimal.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "RangedEnemyProjectile.generated.h"

/**
 * Blueprint parent for arrows fired by ARangedEnemy.
 * DamageData and the optional team filter are inherited from AArrowProjectile;
 * this class is an Enemy-specific extension point without changing target policy.
 */
UCLASS(Blueprintable)
class ENEMY_API ARangedEnemyProjectile : public AArrowProjectile
{
	GENERATED_BODY()

public:
	ARangedEnemyProjectile();
};
