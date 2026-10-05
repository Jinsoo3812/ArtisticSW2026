#pragma once

#include "CoreMinimal.h"

struct ARTISTICSWCORE_API FSWRoomRuntimePaths
{
	static bool TryResolveRoot(FString& OutRoot, FString& OutError);
	static FString GetRoot();
	static FString GetSaveDirectory();
	static FString GetHostDirectory();
};
