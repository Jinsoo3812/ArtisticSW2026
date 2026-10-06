#pragma once

#include "CoreMinimal.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "PlayerArrowProjectile.generated.h"

/**
 * Blueprint parent for arrows fired by Player bows.
 * All movement, embedded DamageData, status effects, and optional team filtering
 * live in AArrowProjectile; this class is a Player-specific extension point.
 */
UCLASS(Blueprintable)
class ARTISTICSWCORE_API APlayerArrowProjectile : public AArrowProjectile
{
	GENERATED_BODY()

public:
	APlayerArrowProjectile();
	virtual FCollisionQueryParams MakeFlightQueryParams() const override;
	/** Detaches after Blueprint construction and applies the committed world velocity without resampling the ship. */
	bool LaunchPlayerShot(const FProjectileShotSnapshot& Shot, AActor* Weapon);
	virtual void HandleFlightImpact(const FHitResult& Hit) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FGuid PlayerShotId;
	FVector LastDebugPosition = FVector::ZeroVector;
	uint64 PlayerLaunchFrame = 0;
	bool bFirstStepLogged = false;
};
