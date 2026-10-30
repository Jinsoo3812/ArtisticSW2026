#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEnemyShipInit, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogEnemyCrew, Log, All);

namespace EnemyShipDiagnostics
{
	ENEMY_API bool InitEnabled();
	ENEMY_API bool CrewEnabled();
	/** 0=off, 1=queue lifecycle/progress, 2=stage transitions as well. */
	ENEMY_API int32 QueueLogLevel();
	/** Wall-clock limits: 12 events/host/second and 120 events/process/second. */
	ENEMY_API bool AdmitCrewEvent(double& WindowStart, int32& Count, uint32& Suppressed);
}
