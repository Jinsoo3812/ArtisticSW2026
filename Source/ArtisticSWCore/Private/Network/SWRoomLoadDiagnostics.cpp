#include "Network/SWRoomLoadDiagnostics.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Network/SWNetworkLog.h"
#include "Room/SWRoomSaveGame.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"

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

namespace
{
void LogFields(const TCHAR* Phase, const FGuid& RoomId, uint64 Sequence, const FString& Owner,
	const UScriptStruct* Type, const void* Data)
{
	if (!IsEnabled()) return;
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property->HasAnyPropertyFlags(CPF_SaveGame)) continue;
		FString Value;
		Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Data), nullptr, nullptr, PPF_None);
		UE_LOG(LogSWRoomSave, Display, TEXT("Flow=StateEvidence Version=20261010 Phase=%s RoomId=%s Sequence=%llu Owner=%s Field=%s Value=%s"),
			Phase, *RoomId.ToString(), Sequence, *Owner, *Property->GetName(), *Value);
	}
}
}

void LogPlayer(const TCHAR* Phase, const FGuid& RoomId, uint64 Sequence, const FString& PlayerKey, const FSWRoomPlayerProgress& Progress)
{
	LogFields(Phase, RoomId, Sequence, TEXT("Player:") + PlayerKey, FSWRoomPlayerProgress::StaticStruct(), &Progress);
}

void LogSnapshot(const TCHAR* Phase, const FGuid& RoomId, const FSWRoomWorldSnapshot& Snapshot)
{
	if (!IsEnabled()) return;
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SnapshotEvidence Version=20261010 Phase=%s RoomId=%s Sequence=%llu Map=%s Actors=%d Unloaded=%d Systems=%d Tombstones=%d Issues=%d"),
		Phase, *RoomId.ToString(), Snapshot.CaptureSequence, *Snapshot.MapPath.ToString(), Snapshot.Actors.Num(),
		Snapshot.UnloadedActors.Num(), Snapshot.Systems.Num(), Snapshot.DestroyedLevelActorIds.Num(), Snapshot.CaptureIssues.Num());
	auto LogActors = [&](const TArray<FSWRoomActorRecord>& Records, const TCHAR* Scope)
	{
		for (const FSWRoomActorRecord& Record : Records)
		{
			UE_LOG(LogSWRoomSave, Display, TEXT("Flow=ActorEvidence Phase=%s RoomId=%s Sequence=%llu Scope=%s Id=%s Class=%s Partition=%s Adapter=%s AdapterVersion=%d Transform=%s Velocity=%s SaveBytes=%d SaveCRC=%u AdapterBytes=%d AdapterCRC=%u Components=%d"),
				Phase, *RoomId.ToString(), Snapshot.CaptureSequence, Scope, *Record.StableId.ToString(), *Record.ClassPath.ToString(),
				*Record.LevelPartition.PackagePath.ToString(), *Record.AdapterType.ToString(), Record.AdapterVersion,
				*Record.WorldTransform.ToString(), *Record.MotionState.LinearVelocity.ToString(), Record.SaveGameBytes.Num(),
				FCrc::MemCrc32(Record.SaveGameBytes.GetData(), Record.SaveGameBytes.Num()), Record.AdapterBytes.Num(),
				FCrc::MemCrc32(Record.AdapterBytes.GetData(), Record.AdapterBytes.Num()), Record.Components.Num());
			for (const FSWRoomComponentRecord& Component : Record.Components)
				UE_LOG(LogSWRoomSave, Display, TEXT("Flow=ComponentEvidence Phase=%s RoomId=%s Sequence=%llu Id=%s Key=%s Transform=%s Bytes=%d CRC=%u"),
					Phase, *RoomId.ToString(), Snapshot.CaptureSequence, *Record.StableId.ToString(), *Component.StableKey.ToString(),
					*Component.WorldTransform.ToString(), Component.SaveGameBytes.Num(), FCrc::MemCrc32(Component.SaveGameBytes.GetData(), Component.SaveGameBytes.Num()));
		}
	};
	LogActors(Snapshot.Actors, TEXT("Loaded"));
	LogActors(Snapshot.UnloadedActors, TEXT("Unloaded"));
	for (const FSWRoomSystemRecord& System : Snapshot.Systems)
		UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SystemEvidence Phase=%s RoomId=%s Sequence=%llu Key=%s Contract=%d Bytes=%d CRC=%u"),
			Phase, *RoomId.ToString(), Snapshot.CaptureSequence, *System.StableKey.ToString(), System.ContractVersion,
			System.SaveGameBytes.Num(), FCrc::MemCrc32(System.SaveGameBytes.GetData(), System.SaveGameBytes.Num()));
}

void LogRoom(const TCHAR* Phase, const USWRoomSaveGame* Room)
{
	if (!IsEnabled() || !Room) return;
	LogSnapshot(Phase, Room->RoomId, Room->WorldSnapshot);
	LogPlayer(Phase, Room->RoomId, Room->CaptureSequence, Room->HostDisplayName, Room->HostProgress);
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
		LogPlayer(Phase, Room->RoomId, Room->CaptureSequence, Guest.DisplayName, Guest.Progress);
	LogFields(Phase, Room->RoomId, Room->CaptureSequence, TEXT("Shared"), FSWRoomSharedProgress::StaticStruct(), &Room->SharedProgress);
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
