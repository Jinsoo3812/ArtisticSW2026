#include "KelvinShip.h"

#include "SWShipWakeEmitterComponent.h"

AKelvinShip::AKelvinShip()
{
	ShipWakeEmitter = CreateDefaultSubobject<USWShipWakeEmitterComponent>(TEXT("ShipWakeEmitter"));
}

