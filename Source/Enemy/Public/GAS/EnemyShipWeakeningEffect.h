#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EnemyShipWeakeningEffect.generated.h"

UCLASS(NotBlueprintable)
class ENEMY_API UEnemyShipWeakeningEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyShipWeakeningEffect();
};
