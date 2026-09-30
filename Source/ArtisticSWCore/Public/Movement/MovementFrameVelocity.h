#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;

namespace MovementFrameVelocity
{
	/** Only a current grounded base or actual attachment counts; never a lifecycle/Owner reference. */
	ARTISTICSWCORE_API USceneComponent* GetCarrier(const AActor* Actor, FName& OutBoneName);
	/** False for a movable, non-physical carrier without a provider; never treat unknown rotation as zero. */
	ARTISTICSWCORE_API bool TryGetPointVelocity(USceneComponent* Carrier, FName BoneName,
		const FVector& WorldPoint, FVector& OutVelocity);
	/** Grounded CharacterMovement velocity excludes carried movement. Falling velocity already includes it. */
	ARTISTICSWCORE_API bool TryGetActorPointVelocity(const AActor* Actor,
		const FVector& WorldPoint, FVector& OutVelocity);
}
