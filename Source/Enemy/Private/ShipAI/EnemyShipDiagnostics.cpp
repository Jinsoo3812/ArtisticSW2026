#include "ShipAI/EnemyShipDiagnostics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(LogEnemyShipInit);
DEFINE_LOG_CATEGORY(LogEnemyCrew);

static TAutoConsoleVariable<int32> CVarShipInitLog(TEXT("sw.EnemyShip.InitLog"), 1,
	TEXT("Initial pool Actor construction timing summaries. No Actor/ISM transition logs."));
static TAutoConsoleVariable<int32> CVarCrewLog(TEXT("sw.EnemyCrew.Log"), 1,
	TEXT("Bounded crew initialization, sight, request, readiness and activation diagnostics."));

// Pool/crew diagnostics are commented out after runtime verification (2026-10-13).
// Keep the gates off as well to avoid formatting, profile scans and timing work.
bool EnemyShipDiagnostics::InitEnabled()
{
	// return CVarShipInitLog.GetValueOnGameThread() != 0;
	return false;
}
bool EnemyShipDiagnostics::CrewEnabled()
{
	// return CVarCrewLog.GetValueOnGameThread() != 0;
	return false;
}

bool EnemyShipDiagnostics::AdmitCrewEvent(double& WindowStart, int32& Count, uint32& Suppressed)
{
	if (!CrewEnabled()) return false;
	static double GlobalWindow = 0.0;
	static int32 GlobalCount = 0;
	const double Now = FPlatformTime::Seconds();
	if (Now - WindowStart >= 1.0) { WindowStart = Now; Count = 0; }
	if (Now - GlobalWindow >= 1.0) { GlobalWindow = Now; GlobalCount = 0; }
	if (Count >= 12 || GlobalCount >= 120) { ++Suppressed; return false; }
	++Count; ++GlobalCount;
	return true;
}
