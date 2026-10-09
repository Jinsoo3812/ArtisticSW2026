#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class AShipBossEnemy;

/** Cosmetic GAS feedback, independent from relocation success, damage and AI decisions. */
namespace BossVanishFeedback
{
	ENEMY_API void ExecuteAtLocation(AShipBossEnemy* Boss, FGameplayTag CueTag, const FVector& Location);
}
