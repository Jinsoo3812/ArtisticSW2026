#pragma once

#include "CoreMinimal.h"

class APlayerController;

// Enemy supplies the implementation without making ClassFeature depend on Enemy.
DECLARE_DELEGATE_RetVal_ThreeParams(FString, FSWEnemyShipDebugTeleport,
	APlayerController*, const FString&, const FString&);

namespace SWEnemyShipDebug
{
	CLASSFEATURE_API FSWEnemyShipDebugTeleport& GetTeleportHandler();
}
