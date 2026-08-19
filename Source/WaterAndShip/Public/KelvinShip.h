#pragma once

#include "CoreMinimal.h"
#include "Ship.h"
#include "KelvinShip.generated.h"

class USWShipWakeEmitterComponent;

/** Player ship variant with a native Kelvin wake emitter inherited by the test Blueprint. */
UCLASS(BlueprintType, Blueprintable)
class WATERANDSHIP_API AKelvinShip : public AShip
{
	GENERATED_BODY()

public:
	AKelvinShip();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship Wake")
	TObjectPtr<USWShipWakeEmitterComponent> ShipWakeEmitter;
};

