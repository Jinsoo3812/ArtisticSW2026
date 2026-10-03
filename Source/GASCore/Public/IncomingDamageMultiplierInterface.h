#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IncomingDamageMultiplierInterface.generated.h"

UINTERFACE(MinimalAPI)
class UIncomingDamageMultiplierInterface : public UInterface
{
	GENERATED_BODY()
};

/** Supplies the target's current multiplier when GAS damage is resolved. */
class GASCORE_API IIncomingDamageMultiplierInterface
{
	GENERATED_BODY()

public:
	virtual float GetIncomingDamageMultiplier() const = 0;
};
