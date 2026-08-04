#pragma once

#include "CoreMinimal.h"
#include "AI/BaseAIController.h"
#include "RangedEnemyAIController.generated.h"

/**
 * Perception defaults for RangedEnemy. High-level combat execution is routed by
 * the shared state Behavior Tree instead of a permanently running fire timer.
 */
UCLASS(Blueprintable)
class ENEMY_API ARangedEnemyAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	ARangedEnemyAIController();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|AI")
	bool IsValidRangedTarget(const AActor* Candidate) const;
};
