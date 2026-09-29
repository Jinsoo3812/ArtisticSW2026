#include "Network/SWNetworkLog.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogSWConnection);
DEFINE_LOG_CATEGORY(LogSWRoom);
DEFINE_LOG_CATEGORY(LogSWRoomSave);

FSWConnectionFileOutputDevice::FSWConnectionFileOutputDevice(bool bInRoomFlow, bool bInSaveTrace)
	: bRoomFlow(bInRoomFlow), bSaveTrace(bInSaveTrace)
{
	Side = IsRunningDedicatedServer() ? TEXT("Server") : TEXT("Client");
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("SWRoom"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (Files.CreateDirectoryTree(*Directory))
	{
		const FString Name = bSaveTrace
			? FString::Printf(TEXT("RoomSave_%s_%u.txt"), *Side, FPlatformProcess::GetCurrentProcessId())
			: bRoomFlow ? FString::Printf(TEXT("RoomFlow_%s_%u.txt"), *Side, FPlatformProcess::GetCurrentProcessId())
			: (Side == TEXT("Server") ? TEXT("Connection_Server.txt") : TEXT("Connection_Client.txt"));
		const FString Path = FPaths::Combine(Directory, Name);
		FileHandle.Reset(Files.OpenWrite(*Path, bRoomFlow || bSaveTrace, bRoomFlow || bSaveTrace));
	}
	if (!FileHandle) WarnOnce();
	else if (bRoomFlow || bSaveTrace) Serialize(TEXT("Flow=Process Phase=Started"), ELogVerbosity::Display,
		bSaveTrace ? LogSWRoomSave.GetCategoryName() : LogSWRoom.GetCategoryName());
}

FSWConnectionFileOutputDevice::~FSWConnectionFileOutputDevice()
{
	if ((bRoomFlow || bSaveTrace) && FileHandle) Serialize(TEXT("Flow=Process Phase=Stopped"), ELogVerbosity::Display,
		bSaveTrace ? LogSWRoomSave.GetCategoryName() : LogSWRoom.GetCategoryName());
	Flush();
}

void FSWConnectionFileOutputDevice::WarnOnce()
{
	if (!bWarned)
	{
		bWarned = true;
			UE_LOG(LogTemp, Warning, TEXT("SWRoom %s file log is unavailable"), bSaveTrace ? TEXT("save")
				: bRoomFlow ? TEXT("flow") : TEXT("connection"));
	}
}

void FSWConnectionFileOutputDevice::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	if (bSaveTrace ? Category != LogSWRoomSave.GetCategoryName()
		: bRoomFlow ? Category != LogSWRoom.GetCategoryName() && Category != LogSWConnection.GetCategoryName()
		: Category != LogSWConnection.GetCategoryName()) return;
	FScopeLock Lock(&Mutex);
	if (!FileHandle) return;
	FString Escaped = Message ? Message : TEXT("");
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	const FString Timestamp = FDateTime::UtcNow().ToIso8601();
	const FString Line = bRoomFlow || bSaveTrace
		? FString::Printf(TEXT("%s | %u | %s | %s | %s | %s\n"),
			*Timestamp, FPlatformProcess::GetCurrentProcessId(), *Side, *Category.ToString(), ToString(Verbosity), *Escaped)
		: FString::Printf(TEXT("%s | %u | %s | %s | %s\n"),
			*Timestamp, FPlatformProcess::GetCurrentProcessId(), *Side, ToString(Verbosity), *Escaped);
	FTCHARToUTF8 Utf8(*Line);
	if (!FileHandle->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length())
		|| !FileHandle->Flush(true))
	{
		FileHandle.Reset();
		WarnOnce();
	}
}

void FSWConnectionFileOutputDevice::Flush()
{
	FScopeLock Lock(&Mutex);
	if (FileHandle && !FileHandle->Flush(true))
	{
		FileHandle.Reset();
		WarnOnce();
	}
}
