#include "Network/SWRoomLoadDiagnostics.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace SWRoomLoadDiagnostics
{
namespace
{
FThreadSafeCounter64 LogWriteFlushCalls;
FThreadSafeCounter64 LogMutexMicroseconds;
FThreadSafeCounter64 LogFormatMicroseconds;
FThreadSafeCounter64 LogWriteMicroseconds;
FThreadSafeCounter64 LogFlushMicroseconds;
FThreadSafeCounter64 LogBytes;
}

bool IsEnabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (IsRunningCommandlet()) return false;
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("SWRoomLoadDiag"));
	return bEnabled;
#endif
}

void Mark(const TCHAR* Phase)
{
	if (IsEnabled())
	{
		TRACE_BOOKMARK(TEXT("SWLoadDiag %s"), Phase);
		UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Pid=%u Real=%.6f Phase=%s"),
			FPlatformProcess::GetCurrentProcessId(), FPlatformTime::Seconds(), Phase);
	}
}

void MarkMemory(const TCHAR* Phase)
{
	if (!IsEnabled()) return;
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Pid=%u Real=%.6f Phase=%s.Memory UsedPhysicalMiB=%.1f UsedVirtualMiB=%.1f AvailablePhysicalMiB=%.1f AvailableVirtualMiB=%.1f"),
		FPlatformProcess::GetCurrentProcessId(), FPlatformTime::Seconds(), Phase,
		Stats.UsedPhysical / 1048576.0, Stats.UsedVirtual / 1048576.0,
		Stats.AvailablePhysical / 1048576.0, Stats.AvailableVirtual / 1048576.0);
}

void RecordLogWriteFlush(double MutexSeconds, double FormatSeconds, double WriteSeconds, double FlushSeconds, int64 Bytes)
{
	LogWriteFlushCalls.Increment();
	LogMutexMicroseconds.Add(static_cast<int64>(MutexSeconds * 1000000.0));
	LogFormatMicroseconds.Add(static_cast<int64>(FormatSeconds * 1000000.0));
	LogWriteMicroseconds.Add(static_cast<int64>(WriteSeconds * 1000000.0));
	LogFlushMicroseconds.Add(static_cast<int64>(FlushSeconds * 1000000.0));
	LogBytes.Add(Bytes);
}

FLogTotals GetLogTotals()
{
	return {LogWriteFlushCalls.GetValue(), LogMutexMicroseconds.GetValue(), LogFormatMicroseconds.GetValue(),
		LogWriteMicroseconds.GetValue(), LogFlushMicroseconds.GetValue(), LogBytes.GetValue()};
}

FScopedPhase::FScopedPhase(const TCHAR* InPhase) : Phase(InPhase)
#if CPUPROFILERTRACE_ENABLED
	, TraceScope(InPhase, IsEnabled())
#endif
{
	if (!IsEnabled()) return;
	StartedAt = FPlatformTime::Seconds();
	InitialTotals = GetLogTotals();
	TRACE_BEGIN_REGION(Phase);
	Mark(InPhase);
}

FScopedPhase::~FScopedPhase()
{
	if (StartedAt == 0.0) return;
	const double FinishedAt = FPlatformTime::Seconds();
	TRACE_END_REGION(Phase);
	const FLogTotals Totals = GetLogTotals();
	UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Pid=%u Real=%.6f Phase=%s.End ElapsedMs=%.3f LogWriteFlushCalls=%lld LogWriteFlushMs=%.3f LogMutexMs=%.3f LogFormatMs=%.3f LogWriteMs=%.3f LogFlushMs=%.3f LogBytes=%lld"),
		FPlatformProcess::GetCurrentProcessId(), FinishedAt, Phase, (FinishedAt - StartedAt) * 1000.0,
		Totals.Calls - InitialTotals.Calls,
		static_cast<double>(Totals.WriteMicroseconds + Totals.FlushMicroseconds - InitialTotals.WriteMicroseconds - InitialTotals.FlushMicroseconds) / 1000.0,
		static_cast<double>(Totals.MutexMicroseconds - InitialTotals.MutexMicroseconds) / 1000.0,
		static_cast<double>(Totals.FormatMicroseconds - InitialTotals.FormatMicroseconds) / 1000.0,
		static_cast<double>(Totals.WriteMicroseconds - InitialTotals.WriteMicroseconds) / 1000.0,
		static_cast<double>(Totals.FlushMicroseconds - InitialTotals.FlushMicroseconds) / 1000.0,
		Totals.Bytes - InitialTotals.Bytes);
}
}
