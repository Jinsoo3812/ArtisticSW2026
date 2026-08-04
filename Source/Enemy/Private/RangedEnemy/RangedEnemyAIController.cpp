#include "RangedEnemy/RangedEnemyAIController.h"

#include "RangedEnemy/RangedEnemy.h"

ARangedEnemyAIController::ARangedEnemyAIController()
{
	SightRadius = 3000.0f;
	LoseSightRadius = 3500.0f;
	PeripheralVisionDegrees = 80.0f;
	SightMaxAge = 2.0f;
}

bool ARangedEnemyAIController::IsValidRangedTarget(const AActor* Candidate) const
{
	const ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	return Enemy && Enemy->IsValidCombatTarget(Candidate);
}
