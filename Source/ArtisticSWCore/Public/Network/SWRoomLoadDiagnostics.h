#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SWRoomLoadDiagnostics
{
ARTISTICSWCORE_API bool IsEnabled();
ARTISTICSWCORE_API void Mark(const TCHAR* Phase);
ARTISTICSWCORE_API void MarkMemory(const TCHAR* Phase);

struct FLogTotals
{
	int64 Calls = 0;
	int64 MutexMicroseconds = 0;
	int64 FormatMicroseconds = 0;
	int64 WriteMicroseconds = 0;
	int64 FlushMicroseconds = 0;
	int64 Bytes = 0;
};
ARTISTICSWCORE_API void RecordLogWriteFlush(double MutexSeconds, double FormatSeconds, double WriteSeconds, double FlushSeconds, int64 Bytes);
ARTISTICSWCORE_API FLogTotals GetLogTotals();

class ARTISTICSWCORE_API FScopedPhase
{
public:
	explicit FScopedPhase(const TCHAR* InPhase);
	~FScopedPhase();

private:
	const TCHAR* Phase;
	double StartedAt = 0.0;
	FLogTotals InitialTotals;
#if CPUPROFILERTRACE_ENABLED
	FCpuProfilerTrace::FDynamicEventScope TraceScope;
#endif
};
}
