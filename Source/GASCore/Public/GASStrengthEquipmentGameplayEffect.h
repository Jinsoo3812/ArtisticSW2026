#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GASStrengthEquipmentGameplayEffect.generated.h"

/** Common infinite equipment effect. Data.StrengthBonus is added to Strength. */
UCLASS()
class GASCORE_API UGASStrengthEquipmentGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGASStrengthEquipmentGameplayEffect();
};
