#include "Storage/StorageInteractionDiagnostics.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogStorageInteraction);

static TAutoConsoleVariable<int32> CVarStorageInteractionLog(
	TEXT("sw.StorageInteractionLog"),
	0,
	TEXT("Log the F-key interaction pipeline through target selection, server handling, and storage UI. 0=off, 1=on."),
	ECVF_Default);

bool IsStorageInteractionLoggingEnabled()
{
	return CVarStorageInteractionLog.GetValueOnGameThread() != 0;
}
