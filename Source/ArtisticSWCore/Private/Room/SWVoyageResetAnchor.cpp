#include "Room/SWVoyageResetAnchor.h"
#include "Components/SceneComponent.h"
#include "Room/SWVoyageResetProfile.h"

ASWVoyageResetAnchor::ASWVoyageResetAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("SWVoyage.Anchor"));
}
