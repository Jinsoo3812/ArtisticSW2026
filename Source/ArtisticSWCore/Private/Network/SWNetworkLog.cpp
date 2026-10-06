#include "Network/SWNetworkLog.h"
#include "Network/SWRoomLoadDiagnostics.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogSWConnection);
DEFINE_LOG_CATEGORY(LogSWRoom);
DEFINE_LOG_CATEGORY(LogSWRoomSave);

bool SWRoomLogging::IsDetailedEnabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (IsRunningCommandlet()) return false;
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("SWRoomDetailedLog"));
	return bEnabled;
#endif
}

FSWConnectionFileOutputDevice::FSWConnectionFileOutputDevice(bool bInRoomFlow, bool bInSaveTrace)
	: bRoomFlow(bInRoomFlow), bSaveTrace(bInSaveTrace)
{
	if (IsRunningCommandlet()) return;
	LastDrainAt = FPlatformTime::Seconds();
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

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
FSWConnectionFileOutputDevice::FSWConnectionFileOutputDevice(TUniquePtr<IFileHandle> InFileHandle)
	: FileHandle(MoveTemp(InFileHandle)), LastDrainAt(FPlatformTime::Seconds()), Side(TEXT("Test"))
{
}
#endif

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
	const bool bDiagnostic = SWRoomLoadDiagnostics::IsEnabled();
	TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(SWLog_Serialize, bDiagnostic);
	const double BeforeMutex = bDiagnostic ? FPlatformTime::Seconds() : 0.0;
	FScopeLock Lock(&Mutex);
	const double AfterMutex = bDiagnostic ? FPlatformTime::Seconds() : 0.0;
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
	PendingBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	PendingMutexSeconds += AfterMutex - BeforeMutex;
	PendingFormatSeconds += bDiagnostic ? FPlatformTime::Seconds() - AfterMutex : 0.0;
	const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	const bool bUrgent = Level <= ELogVerbosity::Error
		|| (Level == ELogVerbosity::Warning && !SWRoomLogging::IsDetailedEnabled());
	const bool bDrain = bUrgent || PendingBytes.Num() >= 64 * 1024
		|| FPlatformTime::Seconds() - LastDrainAt >= 1.0;
	bool bWritten = true;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(SWLog_Write, bDiagnostic);
		if (bDrain) bWritten = DrainBuffer(bUrgent);
	}
	if (!bWritten)
	{
		FileHandle.Reset();
		PendingBytes.Reset();
		WarnOnce();
	}
}

bool FSWConnectionFileOutputDevice::DrainBuffer(bool bFullFlush)
{
	// Mutex is held by Serialize/Flush. Warnings and errors preserve recent context immediately.
	if (!FileHandle) return false;
	const bool bDiagnostic = SWRoomLoadDiagnostics::IsEnabled();
	const int32 Bytes = PendingBytes.Num();
	const double BeforeWrite = bDiagnostic ? FPlatformTime::Seconds() : 0.0;
	bool bWritten = true;
	if (!PendingBytes.IsEmpty())
	{
		bWritten = FileHandle->Write(PendingBytes.GetData(), PendingBytes.Num());
		if (bWritten) PendingBytes.Reset();
	}
	const double BeforeFlush = bDiagnostic ? FPlatformTime::Seconds() : 0.0;
	const bool bFlushed = bWritten && FileHandle->Flush(bFullFlush);
	const double AfterFlush = bDiagnostic ? FPlatformTime::Seconds() : 0.0;
	if (bDiagnostic)
		SWRoomLoadDiagnostics::RecordLogWriteFlush(PendingMutexSeconds, PendingFormatSeconds,
			BeforeFlush - BeforeWrite, AfterFlush - BeforeFlush, Bytes);
	PendingMutexSeconds = 0.0;
	PendingFormatSeconds = 0.0;
	LastDrainAt = FPlatformTime::Seconds();
	return bFlushed;
}

void FSWConnectionFileOutputDevice::FlushBuffered()
{
	FScopeLock Lock(&Mutex);
	if (FileHandle && !PendingBytes.IsEmpty() && !DrainBuffer(false))
	{
		FileHandle.Reset();
		PendingBytes.Reset();
		WarnOnce();
	}
}

void FSWConnectionFileOutputDevice::Flush()
{
	FScopeLock Lock(&Mutex);
	if (FileHandle && !DrainBuffer(true))
	{
		FileHandle.Reset();
		PendingBytes.Reset();
		WarnOnce();
	}
}
