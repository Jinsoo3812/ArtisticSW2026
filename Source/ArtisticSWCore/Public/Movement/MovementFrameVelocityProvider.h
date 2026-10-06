#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MovementFrameVelocityProvider.generated.h"

/** Implemented by carriers whose walking surface is not their physics body. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMovementFrameVelocityProvider : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API IMovementFrameVelocityProvider
{
	GENERATED_BODY()

public:
	/** World cm/s at WorldPoint, including rotation. False is unavailable, not stationary. */
	virtual bool TryGetMovementFrameVelocityAtPoint(const FVector& WorldPoint, FVector& OutVelocity) const = 0;
};
