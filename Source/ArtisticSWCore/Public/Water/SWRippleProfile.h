#pragma once

#include "CoreMinimal.h"

/**
 * Cumulative, process-local counters used by the deterministic Ripple profiling map.
 * Recording is command-line gated by -SWProfileRipple so normal gameplay pays only
 * one cached branch at each instrumentation point.
 */
struct ARTISTICSWCORE_API FSWRippleProfileSnapshot
{
	int64 EvaluationCalls = 0;
	int64 EventsScanned = 0;
	int64 ActiveEventsEvaluated = 0;
	int64 EnvelopeEvaluations = 0;
	int64 FullSnapshotCalls = 0;
	int64 FullSnapshotBytes = 0;
	int64 FullSnapshotCycles = 0;
	int64 ActiveSnapshotCalls = 0;
	int64 ActiveSnapshotBytes = 0;
	int64 ActiveSnapshotCycles = 0;
	int64 QueryBatchCalls = 0;
	int64 QueryBatchCycles = 0;
	int64 TextureUpdateCalls = 0;
	int64 TextureUpdateCycles = 0;
	int64 TextureUploadEnqueues = 0;
	int64 TextureUploadBytes = 0;
	int64 TextureUpdatesWithoutRevisionChange = 0;
	int64 MaterialBindPasses = 0;
	int64 MaterialBindCycles = 0;
	int64 WaterBodiesVisited = 0;
	int64 MaterialParameterWrites = 0;
	int64 AuthoritativeEventsAdded = 0;
	int64 ReplicatedEventsApplied = 0;
	int64 ReplicatedEventsRemoved = 0;
};

class ARTISTICSWCORE_API FSWRippleProfile
{
public:
	static bool IsEnabled();
	static FSWRippleProfileSnapshot Capture();

	static void RecordEvaluation(int32 EventsScanned, int32 ActiveEvents, int32 EnvelopeEvaluations);
	static void RecordFullSnapshot(int32 EventCount, uint64 Cycles);
	static void RecordActiveSnapshot(int32 EventCount, uint64 Cycles);
	static void RecordQueryBatch(uint64 Cycles);
	static void RecordState(int32 EventCount, uint32 Revision);
	static void RecordTextureUpdate(int32 ActiveEventCount, uint32 Revision, bool bRevisionUnchanged);
	static void RecordTextureUpdateCycles(uint64 Cycles);
	static void RecordTextureUpload(int32 ByteCount);
	static void RecordMaterialBind(int32 WaterBodyCount, int32 ParameterWriteCount, uint64 Cycles);
	static void RecordAuthoritativeEventAdded();
	static void RecordReplicatedEventApplied();
	static void RecordReplicatedEventsRemoved(int32 Count);
};
