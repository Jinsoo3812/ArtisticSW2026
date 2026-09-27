#include "Network/SWNetworkLog.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogSWConnection);

FSWConnectionFileOutputDevice::FSWConnectionFileOutputDevice()
{
	Side = IsRunningDedicatedServer() ? TEXT("Server") : TEXT("Client");
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("SWRoom"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (Files.CreateDirectoryTree(*Directory))
	{
		const FString Path = FPaths::Combine(Directory,
			Side == TEXT("Server") ? TEXT("Connection_Server.txt") : TEXT("Connection_Client.txt"));
		FileHandle.Reset(Files.OpenWrite(*Path, false, false));
	}
	if (!FileHandle) WarnOnce();
}

FSWConnectionFileOutputDevice::~FSWConnectionFileOutputDevice()
{
	Flush();
}

void FSWConnectionFileOutputDevice::WarnOnce()
{
	if (!bWarned)
	{
		bWarned = true;
		UE_LOG(LogTemp, Warning, TEXT("SWRoom connection file log is unavailable"));
	}
}

void FSWConnectionFileOutputDevice::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	if (Category != LogSWConnection.GetCategoryName()) return;
	FScopeLock Lock(&Mutex);
	if (!FileHandle) return;
	FString Escaped = Message ? Message : TEXT("");
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	const FString Line = FString::Printf(TEXT("%s | %u | %s | %s | %s\n"),
		*FDateTime::UtcNow().ToIso8601(), FPlatformProcess::GetCurrentProcessId(), *Side,
		ToString(Verbosity), *Escaped);
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
