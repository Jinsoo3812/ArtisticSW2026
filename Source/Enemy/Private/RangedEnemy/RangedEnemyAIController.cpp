#include "RangedEnemy/RangedEnemyAIController.h"

#include "RangedEnemy/RangedEnemy.h"

ARangedEnemyAIController::ARangedEnemyAIController()
{
}

bool ARangedEnemyAIController::IsValidRangedTarget(const AActor* Candidate) const
{
	const ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	return Enemy && Enemy->IsValidCombatTarget(Candidate);
}
