#include "PlayerRespawnPoint.h"
#include "Components/ArrowComponent.h"

APlayerRespawnPoint::APlayerRespawnPoint()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);
	bReplicates = false;
}
