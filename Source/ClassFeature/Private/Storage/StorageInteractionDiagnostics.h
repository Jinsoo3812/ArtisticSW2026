#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStorageInteraction, Log, All);

/** Console command: sw.StorageInteractionLog 1 (0 to disable). */
bool IsStorageInteractionLoggingEnabled();
