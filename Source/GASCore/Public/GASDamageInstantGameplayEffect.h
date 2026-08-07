#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GASDamageInstantGameplayEffect.generated.h"

/** Common instant damage effect. Data.Damage is added only to the Damage meta attribute. */
UCLASS()
class GASCORE_API UGASDamageInstantGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGASDamageInstantGameplayEffect();
};
