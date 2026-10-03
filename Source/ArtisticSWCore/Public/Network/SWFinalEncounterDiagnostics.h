#pragma once

#include "CoreMinimal.h"

struct ARTISTICSWCORE_API FSWFinalEncounterDiagnostics
{
	static void Write(const TCHAR* Flow, const TCHAR* Phase, const FString& Fields);
};
