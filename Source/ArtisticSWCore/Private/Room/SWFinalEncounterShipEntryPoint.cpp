#include "Room/SWFinalEncounterShipEntryPoint.h"
#include "Components/ArrowComponent.h"

ASWFinalEncounterShipEntryPoint::ASWFinalEncounterShipEntryPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SetActorEnableCollision(false);
	Direction = CreateDefaultSubobject<UArrowComponent>(TEXT("Direction"));
	SetRootComponent(Direction);
	Direction->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
