#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GASAttributeDamageGameplayEffect.generated.h"

/** Native instant GE that delegates final weapon damage to an execution calculation. */
UCLASS()
class GASCORE_API UGASAttributeDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGASAttributeDamageGameplayEffect();
};
