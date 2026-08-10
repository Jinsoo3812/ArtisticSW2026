#pragma once

#include "CoreMinimal.h"
#include "Cannonball.h"
#include "EnemyShipTorpedo.generated.h"

class AShip;

/** Dedicated Enemy Ship projectile: direct Player Ship damage, no area damage. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTorpedo : public ACannonball
{
	GENERATED_BODY()

public:
	AEnemyShipTorpedo();

	void InitializeTorpedo(
		AShip* InLaunchingShip,
		AShip* InDesignatedTarget,
		float InSnapshotDamage,
		float InSpeed,
		const FVector& InDesignatedImpactLocation,
		float InImpactTolerance,
		float InMaximumFlightSeconds);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Torpedo")
	float GetSnapshotDamage() const { return DamageAmount; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Torpedo")
	AShip* GetDesignatedTarget() const { return DesignatedTarget.Get(); }

protected:
	virtual void HandleShipHit(AShip* HitShip) override;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastTorpedoExploded(const FVector& ExplosionLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy Ship|Torpedo", meta = (DisplayName = "On Torpedo Exploded"))
	void K2_OnTorpedoExploded(const FVector& ExplosionLocation);

private:
	TWeakObjectPtr<AShip> DesignatedTarget;
};
