#pragma once

#include "CoreMinimal.h"

class UGameInstance;

class ARTISTICSW2026_API FSWInputDiag
{
public:
	static void BeginAttempt(UGameInstance* Instance, int32 AttemptId, bool bHost);
	static void Record(UGameInstance* Instance, const TCHAR* Event);
	static void RecordFirstMove(UGameInstance* Instance);
	static void RecordFirstLook(UGameInstance* Instance);
};
