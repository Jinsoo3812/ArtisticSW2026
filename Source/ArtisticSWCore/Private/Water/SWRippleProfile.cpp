#include "Water/SWRippleProfile.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Water/SWRippleTypes.h"

namespace
{
	struct FSWRippleProfileAtomicCounters
	{
		TAtomic<int64> EvaluationCalls { 0 };
		TAtomic<int64> EventsScanned { 0 };
		TAtomic<int64> ActiveEventsEvaluated { 0 };
		TAtomic<int64> EnvelopeEvaluations { 0 };
		TAtomic<int64> FullSnapshotCalls { 0 };
		TAtomic<int64> FullSnapshotBytes { 0 };
		TAtomic<int64> FullSnapshotCycles { 0 };
		TAtomic<int64> ActiveSnapshotCalls { 0 };
		TAtomic<int64> ActiveSnapshotBytes { 0 };
		TAtomic<int64> ActiveSnapshotCycles { 0 };
		TAtomic<int64> QueryBatchCalls { 0 };
		TAtomic<int64> QueryBatchCycles { 0 };
		TAtomic<int64> TextureUpdateCalls { 0 };
		TAtomic<int64> TextureUpdateCycles { 0 };
		TAtomic<int64> TextureUploadEnqueues { 0 };
		TAtomic<int64> TextureUploadBytes { 0 };
		TAtomic<int64> TextureUpdatesWithoutRevisionChange { 0 };
		TAtomic<int64> MaterialBindPasses { 0 };
		TAtomic<int64> MaterialBindCycles { 0 };
		TAtomic<int64> WaterBodiesVisited { 0 };
		TAtomic<int64> MaterialParameterWrites { 0 };
		TAtomic<int64> AuthoritativeEventsAdded { 0 };
		TAtomic<int64> ReplicatedEventsApplied { 0 };
		TAtomic<int64> ReplicatedEventsRemoved { 0 };
	};

	FSWRippleProfileAtomicCounters GCounters;

	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleEvaluationCalls, TEXT("SW/Ripple/EvaluationCalls"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleEventsScanned, TEXT("SW/Ripple/EventsScanned"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleActiveEventsEvaluated, TEXT("SW/Ripple/ActiveEventsEvaluated"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleEnvelopeEvaluations, TEXT("SW/Ripple/EnvelopeEvaluations"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleFullSnapshotCalls, TEXT("SW/Ripple/FullSnapshotCalls"));
	TRACE_DECLARE_ATOMIC_MEMORY_COUNTER(SWRippleFullSnapshotBytes, TEXT("SW/Ripple/FullSnapshotBytes"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleActiveSnapshotCalls, TEXT("SW/Ripple/ActiveSnapshotCalls"));
	TRACE_DECLARE_ATOMIC_MEMORY_COUNTER(SWRippleActiveSnapshotBytes, TEXT("SW/Ripple/ActiveSnapshotBytes"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleTextureUpdateCalls, TEXT("SW/Ripple/TextureUpdateCalls"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleTextureUploadEnqueues, TEXT("SW/Ripple/TextureUploadEnqueues"));
	TRACE_DECLARE_ATOMIC_MEMORY_COUNTER(SWRippleTextureUploadBytes, TEXT("SW/Ripple/TextureUploadBytes"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleTextureUpdatesWithoutRevisionChange, TEXT("SW/Ripple/TextureUpdatesWithoutRevisionChange"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleMaterialBindPasses, TEXT("SW/Ripple/MaterialBindPasses"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleWaterBodiesVisited, TEXT("SW/Ripple/WaterBodiesVisited"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleMaterialParameterWrites, TEXT("SW/Ripple/MaterialParameterWrites"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleAuthoritativeEventsAdded, TEXT("SW/Ripple/AuthoritativeEventsAdded"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleReplicatedEventsApplied, TEXT("SW/Ripple/ReplicatedEventsApplied"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleReplicatedEventsRemoved, TEXT("SW/Ripple/ReplicatedEventsRemoved"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleStoredEvents, TEXT("SW/Ripple/StoredEvents"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleRevision, TEXT("SW/Ripple/Revision"));
	TRACE_DECLARE_ATOMIC_INT_COUNTER(SWRippleActiveTextureEvents, TEXT("SW/Ripple/ActiveTextureEvents"));

	void Add(TAtomic<int64>& Counter, int64 Value)
	{
		Counter += Value;
	}
}

bool FSWRippleProfile::IsEnabled()
{
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("SWProfileRipple"));
	return bEnabled;
}

FSWRippleProfileSnapshot FSWRippleProfile::Capture()
{
	FSWRippleProfileSnapshot Result;
	Result.EvaluationCalls = GCounters.EvaluationCalls.Load();
	Result.EventsScanned = GCounters.EventsScanned.Load();
	Result.ActiveEventsEvaluated = GCounters.ActiveEventsEvaluated.Load();
	Result.EnvelopeEvaluations = GCounters.EnvelopeEvaluations.Load();
	Result.FullSnapshotCalls = GCounters.FullSnapshotCalls.Load();
	Result.FullSnapshotBytes = GCounters.FullSnapshotBytes.Load();
	Result.FullSnapshotCycles = GCounters.FullSnapshotCycles.Load();
	Result.ActiveSnapshotCalls = GCounters.ActiveSnapshotCalls.Load();
	Result.ActiveSnapshotBytes = GCounters.ActiveSnapshotBytes.Load();
	Result.ActiveSnapshotCycles = GCounters.ActiveSnapshotCycles.Load();
	Result.QueryBatchCalls = GCounters.QueryBatchCalls.Load();
	Result.QueryBatchCycles = GCounters.QueryBatchCycles.Load();
	Result.TextureUpdateCalls = GCounters.TextureUpdateCalls.Load();
	Result.TextureUpdateCycles = GCounters.TextureUpdateCycles.Load();
	Result.TextureUploadEnqueues = GCounters.TextureUploadEnqueues.Load();
	Result.TextureUploadBytes = GCounters.TextureUploadBytes.Load();
	Result.TextureUpdatesWithoutRevisionChange = GCounters.TextureUpdatesWithoutRevisionChange.Load();
	Result.MaterialBindPasses = GCounters.MaterialBindPasses.Load();
	Result.MaterialBindCycles = GCounters.MaterialBindCycles.Load();
	Result.WaterBodiesVisited = GCounters.WaterBodiesVisited.Load();
	Result.MaterialParameterWrites = GCounters.MaterialParameterWrites.Load();
	Result.AuthoritativeEventsAdded = GCounters.AuthoritativeEventsAdded.Load();
	Result.ReplicatedEventsApplied = GCounters.ReplicatedEventsApplied.Load();
	Result.ReplicatedEventsRemoved = GCounters.ReplicatedEventsRemoved.Load();
	return Result;
}

void FSWRippleProfile::RecordEvaluation(int32 InEventsScanned, int32 ActiveEvents, int32 InEnvelopeEvaluations)
{
	if (!IsEnabled())
	{
		return;
	}

	++GCounters.EvaluationCalls;
	Add(GCounters.EventsScanned, InEventsScanned);
	Add(GCounters.ActiveEventsEvaluated, ActiveEvents);
	Add(GCounters.EnvelopeEvaluations, InEnvelopeEvaluations);
	TRACE_COUNTER_INCREMENT(SWRippleEvaluationCalls);
	TRACE_COUNTER_ADD(SWRippleEventsScanned, InEventsScanned);
	TRACE_COUNTER_ADD(SWRippleActiveEventsEvaluated, ActiveEvents);
	TRACE_COUNTER_ADD(SWRippleEnvelopeEvaluations, InEnvelopeEvaluations);
}

void FSWRippleProfile::RecordFullSnapshot(int32 EventCount, uint64 Cycles)
{
	if (!IsEnabled())
	{
		return;
	}

	const int64 ByteCount = static_cast<int64>(EventCount) * sizeof(FSWRippleEvent);
	++GCounters.FullSnapshotCalls;
	Add(GCounters.FullSnapshotBytes, ByteCount);
	Add(GCounters.FullSnapshotCycles, static_cast<int64>(Cycles));
	TRACE_COUNTER_INCREMENT(SWRippleFullSnapshotCalls);
	TRACE_COUNTER_ADD(SWRippleFullSnapshotBytes, ByteCount);
}

void FSWRippleProfile::RecordActiveSnapshot(int32 EventCount, uint64 Cycles)
{
	if (!IsEnabled())
	{
		return;
	}

	const int64 ByteCount = static_cast<int64>(EventCount) * sizeof(FSWRippleEvent);
	++GCounters.ActiveSnapshotCalls;
	Add(GCounters.ActiveSnapshotBytes, ByteCount);
	Add(GCounters.ActiveSnapshotCycles, static_cast<int64>(Cycles));
	TRACE_COUNTER_INCREMENT(SWRippleActiveSnapshotCalls);
	TRACE_COUNTER_ADD(SWRippleActiveSnapshotBytes, ByteCount);
}

void FSWRippleProfile::RecordQueryBatch(uint64 Cycles)
{
	if (!IsEnabled())
	{
		return;
	}
	++GCounters.QueryBatchCalls;
	Add(GCounters.QueryBatchCycles, static_cast<int64>(Cycles));
}

void FSWRippleProfile::RecordState(int32 EventCount, uint32 InRevision)
{
	if (!IsEnabled())
	{
		return;
	}

	TRACE_COUNTER_SET_IF_DIFFERENT(SWRippleStoredEvents, EventCount);
	TRACE_COUNTER_SET_IF_DIFFERENT(SWRippleRevision, static_cast<int64>(InRevision));
}

void FSWRippleProfile::RecordTextureUpdate(int32 ActiveEventCount, uint32 InRevision, bool bRevisionUnchanged)
{
	if (!IsEnabled())
	{
		return;
	}

	++GCounters.TextureUpdateCalls;
	TRACE_COUNTER_INCREMENT(SWRippleTextureUpdateCalls);
	TRACE_COUNTER_SET_IF_DIFFERENT(SWRippleActiveTextureEvents, ActiveEventCount);
	TRACE_COUNTER_SET_IF_DIFFERENT(SWRippleRevision, static_cast<int64>(InRevision));
	if (bRevisionUnchanged)
	{
		++GCounters.TextureUpdatesWithoutRevisionChange;
		TRACE_COUNTER_INCREMENT(SWRippleTextureUpdatesWithoutRevisionChange);
	}
}

void FSWRippleProfile::RecordTextureUpdateCycles(uint64 Cycles)
{
	if (IsEnabled())
	{
		Add(GCounters.TextureUpdateCycles, static_cast<int64>(Cycles));
	}
}

void FSWRippleProfile::RecordTextureUpload(int32 ByteCount)
{
	if (!IsEnabled())
	{
		return;
	}

	++GCounters.TextureUploadEnqueues;
	Add(GCounters.TextureUploadBytes, ByteCount);
	TRACE_COUNTER_INCREMENT(SWRippleTextureUploadEnqueues);
	TRACE_COUNTER_ADD(SWRippleTextureUploadBytes, ByteCount);
}

void FSWRippleProfile::RecordMaterialBind(int32 WaterBodyCount, int32 ParameterWriteCount, uint64 Cycles)
{
	if (!IsEnabled())
	{
		return;
	}

	++GCounters.MaterialBindPasses;
	Add(GCounters.MaterialBindCycles, static_cast<int64>(Cycles));
	Add(GCounters.WaterBodiesVisited, WaterBodyCount);
	Add(GCounters.MaterialParameterWrites, ParameterWriteCount);
	TRACE_COUNTER_INCREMENT(SWRippleMaterialBindPasses);
	TRACE_COUNTER_ADD(SWRippleWaterBodiesVisited, WaterBodyCount);
	TRACE_COUNTER_ADD(SWRippleMaterialParameterWrites, ParameterWriteCount);
}

void FSWRippleProfile::RecordAuthoritativeEventAdded()
{
	if (!IsEnabled())
	{
		return;
	}
	++GCounters.AuthoritativeEventsAdded;
	TRACE_COUNTER_INCREMENT(SWRippleAuthoritativeEventsAdded);
}

void FSWRippleProfile::RecordReplicatedEventApplied()
{
	if (!IsEnabled())
	{
		return;
	}
	++GCounters.ReplicatedEventsApplied;
	TRACE_COUNTER_INCREMENT(SWRippleReplicatedEventsApplied);
}

void FSWRippleProfile::RecordReplicatedEventsRemoved(int32 Count)
{
	if (!IsEnabled() || Count <= 0)
	{
		return;
	}
	Add(GCounters.ReplicatedEventsRemoved, Count);
	TRACE_COUNTER_ADD(SWRippleReplicatedEventsRemoved, Count);
}
