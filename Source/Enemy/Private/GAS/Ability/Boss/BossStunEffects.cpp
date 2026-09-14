#include "GAS/Ability/Boss/BossStunEffects.h"

UBossHeadHitStunEffect::UBossHeadHitStunEffect()
{
	DurationMagnitude = FScalableFloat(2.f);
}

UBossHealthThresholdStunEffect::UBossHealthThresholdStunEffect()
{
	DurationMagnitude = FScalableFloat(3.f);
}
