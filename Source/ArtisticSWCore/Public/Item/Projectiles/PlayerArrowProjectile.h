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
};
