#include "Item/Projectiles/ProjectileLaunchInitialization.h"

#include "GameFramework/ProjectileMovementComponent.h"

bool ProjectileLaunchInitialization::ApplyWorldVelocity(UProjectileMovementComponent* Movement,
	const FVector& WorldVelocity, double MaxWorldSpeed)
{
	if (!IsValid(Movement) || WorldVelocity.ContainsNaN() || !FMath::IsFinite(MaxWorldSpeed)
		|| MaxWorldSpeed < 0.0) return false;
	// InitialSpeed/local-space conversion must not reinterpret an already solved velocity.
	Movement->InitialSpeed = 0.0f;
	Movement->bInitialVelocityInLocalSpace = false;
	Movement->MaxSpeed = MaxWorldSpeed;
	Movement->Velocity = WorldVelocity;
	Movement->UpdateComponentVelocity();
	return true;
}
