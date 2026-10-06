#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MountedDamageUserInterface.generated.h"

UINTERFACE(MinimalAPI)
class UMountedDamageUserInterface : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API IMountedDamageUserInterface
{
	GENERATED_BODY()

public:
	virtual void SetMountedDamageMode(bool bEnabled) = 0;
	virtual bool IsMountedForDamage() const = 0;
	virtual void PrepareForHealthDeath() = 0;
};
