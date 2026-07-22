#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "BasicMeleeDamageGameplayEffect.generated.h"

/** Native fallback effect for melee attacks using Data.Damage as SetByCaller. */
UCLASS()
class CLASSFEATURE_API UBasicMeleeDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBasicMeleeDamageGameplayEffect();
};
