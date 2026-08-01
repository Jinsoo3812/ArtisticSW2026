#pragma once

#include "CoreMinimal.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "RangedEnemyProjectile.generated.h"

/**
 * Projectile used by ARangedEnemy.
 * It still impacts world geometry and friendly actors, but damage is only applied
 * to actors on the Player team. This keeps collision feedback deterministic while
 * preventing friendly fire and ship damage in the first MVP.
 */
UCLASS(Blueprintable)
class ENEMY_API ARangedEnemyProjectile : public AArrowProjectile
{
	GENERATED_BODY()

public:
	ARangedEnemyProjectile();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Projectile")
	bool IsValidDamageTarget(const AActor* TargetActor) const;

protected:
	virtual bool CanApplyDamageToActor(const AActor* OtherActor) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Projectile")
	bool bOnlyDamagePlayers = true;
};
