#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace EnemyShipDebug
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	FString ExecuteTeleport(APlayerController* Controller, const FString& ShipTag, const FString& ArrivalTag);
#endif
}
