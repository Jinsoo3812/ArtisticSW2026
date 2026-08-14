#include "MeleeEnemy/MeleeEnemy.h"

#include "AI/BaseAIController.h"
#include "GameFramework/CharacterMovementComponent.h"

AMeleeEnemy::AMeleeEnemy()
{
	AIControllerClass = ABaseAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// AAIController::SetFocus updates ControlRotation. The melee pawn must consume
	// that yaw while strafing instead of rotating toward movement velocity.
	bUseControllerRotationYaw = true;
	bEquipWeaponOnSpawn = false;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->bOrientRotationToMovement = false;
		MovementComponent->bUseControllerDesiredRotation = false;
	}
}
