#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CannonRiderInterface.generated.h"

UINTERFACE(MinimalAPI)
class UCannonRiderInterface : public UInterface
{
	GENERATED_BODY()
};

class WATERANDSHIP_API ICannonRiderInterface
{
	GENERATED_BODY()

public:
	/** Clears character-specific movement and animation state when cannon control begins. */
	virtual void PrepareForCannonControl() = 0;
};
