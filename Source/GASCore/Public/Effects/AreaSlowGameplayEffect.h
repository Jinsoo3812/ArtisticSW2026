#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AreaSlowGameplayEffect.generated.h"

/**
 * Duration effect used by the player's one-shot rectangular area slow.
 * Duration, movement/attack multipliers, and the granted slow-state tag are
 * supplied by the ability spec so one effect class can serve multiple data assets.
 */
UCLASS(BlueprintType)
class GASCORE_API UAreaSlowGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAreaSlowGameplayEffect();
};
