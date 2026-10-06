#pragma once

#include "CoreMinimal.h"

class UProjectileMovementComponent;

namespace ProjectileLaunchInitialization
{
	/** Final world velocity only. Does not choose aim, add carrier motion, activate, or change gravity/bounce. */
	ARTISTICSWCORE_API bool ApplyWorldVelocity(UProjectileMovementComponent* Movement,
		const FVector& WorldVelocity, double MaxWorldSpeed = 0.0);
}
