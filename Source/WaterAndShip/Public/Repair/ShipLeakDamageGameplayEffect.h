#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "ShipLeakDamageGameplayEffect.generated.h"

/** Instant SetByCaller damage used by the ship's authoritative leak timer. */
UCLASS()
class WATERANDSHIP_API UShipLeakDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UShipLeakDamageGameplayEffect();
};
