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
	virtual FCollisionQueryParams MakeFlightQueryParams() const override;
	/** Applies the committed socket transform after BP construction and damage initialization. */
	bool LaunchEnemyShot(const FProjectileShotSnapshot& Shot, AActor* Weapon);
	virtual void HandleFlightImpact(const FHitResult& Hit) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FGuid EnemyShotId;
	FVector LastDebugPosition = FVector::ZeroVector;
	uint64 EnemyLaunchFrame = 0;
	bool bFirstStepLogged = false;
};
