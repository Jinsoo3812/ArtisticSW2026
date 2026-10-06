#include "Room/SWLevelEntryPoint.h"
#include "Components/ArrowComponent.h"

ASWLevelEntryPoint::ASWLevelEntryPoint()
{
	Direction = CreateDefaultSubobject<UArrowComponent>(TEXT("Direction"));
	SetRootComponent(Direction);
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = false;
}
