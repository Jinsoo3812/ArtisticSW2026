#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "WaterBombEffects.generated.h"

/** 기본 제공 물폭탄 공격속도 GE. 지속시간과 배율은 발사체가 Spec에 주입합니다. */
UCLASS(BlueprintType)
class WATERANDSHIP_API UWaterBombAttackSpeedGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UWaterBombAttackSpeedGameplayEffect();
};

/** 기본 제공 물폭탄 대포 봉쇄 GE. 발사체가 지속시간과 봉쇄 태그를 Spec에 주입합니다. */
UCLASS(BlueprintType)
class WATERANDSHIP_API UWaterBombCannonDisableGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UWaterBombCannonDisableGameplayEffect();
};
