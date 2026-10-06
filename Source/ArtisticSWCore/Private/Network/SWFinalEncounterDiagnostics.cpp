#include "Network/SWFinalEncounterDiagnostics.h"

#include "Network/SWNetworkLog.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FSWFinalEncounterDiagnostics::Write(const TCHAR* Flow, const TCHAR* Phase, const FString& Fields)
{
	check(IsInGameThread());
	const uint32 ProcessId = FPlatformProcess::GetCurrentProcessId();
	const FString Line = FString::Printf(TEXT("UTC=%s PID=%u Flow=%s Phase=%s %s"),
		*FDateTime::UtcNow().ToIso8601(), ProcessId, Flow, Phase, *Fields);
	UE_LOG(LogSWRoom, Display, TEXT("%s"), *Line);

	const FString Directory = FPaths::ProjectLogDir() / TEXT("SWRoom");
	const FString Path = Directory / FString::Printf(TEXT("FinalEncounter-%u.log"), ProcessId);
	static bool bFailureReported = false;
	if ((!IFileManager::Get().MakeDirectory(*Directory, true)
		|| !FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append))
		&& !bFailureReported)
	{
		bFailureReported = true;
		UE_LOG(LogSWRoom, Warning, TEXT("Final encounter diagnostic file append failed: %s"), *Path);
	}
}
