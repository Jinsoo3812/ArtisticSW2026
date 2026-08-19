#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/GA_BasicAttack.h"
#include "GA_BossBasicAttack.generated.h"

/** Existing weapon hit-scan attack with the boss-wide mutual-exclusion state. */
UCLASS()
class ENEMY_API UGA_BossBasicAttack : public UGA_BasicAttack
{
	GENERATED_BODY()

public:
	UGA_BossBasicAttack();
};
