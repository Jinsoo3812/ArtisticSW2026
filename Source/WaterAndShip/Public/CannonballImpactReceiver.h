#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CannonballImpactReceiver.generated.h"

UINTERFACE(MinimalAPI)
class UCannonballImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by blocking actors that need to react to a cannonball sweep impact. */
class WATERANDSHIP_API ICannonballImpactReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Cannonball|Impact")
	void ReceiveCannonballImpact(AActor* CannonballActor);
};
