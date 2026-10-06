#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossAttackPositionLibrary.generated.h"

class AShipBossEnemy;

/** Geometry-only attack readiness. Does not query GAS busy/cooldown state during a committed ability. */
UCLASS()
class ENEMY_API UBossAttackPositionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	static bool CanMeleeAttackFromCurrentPosition(const AShipBossEnemy* Boss, AActor* Target, float AttackRangeInset = 0.f);
};
