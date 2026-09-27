#pragma once
#include "CoreMinimal.h"
#include "Effects/StatusGameplayEffect.h"
#include "BossStunEffects.generated.h"

UCLASS(Blueprintable)
class ENEMY_API UBossHeadHitStunEffect : public UStunGameplayEffect
{
	GENERATED_BODY()
public:
	UBossHeadHitStunEffect();
};

UCLASS(Blueprintable)
class ENEMY_API UBossHealthThresholdStunEffect : public UStunGameplayEffect
{
	GENERATED_BODY()
public:
	UBossHealthThresholdStunEffect();
};
