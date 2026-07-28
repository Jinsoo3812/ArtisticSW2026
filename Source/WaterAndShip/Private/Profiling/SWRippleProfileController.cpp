#include "Profiling/SWRippleProfileController.h"

#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Water/SWRippleStateSubsystem.h"
#include "Water/SWRippleTypes.h"

namespace
{
	int64 Delta(int64 EndValue, int64 StartValue)
	{
		return EndValue - StartValue;
	}
}

ASWRippleProfileController::ASWRippleProfileController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = false;
}

void ASWRippleProfileController::BeginPlay()
{
	Super::BeginPlay();
	ParseCommandLineOverrides();
	BuildQueryPositions();
	// WarmupSeconds is an absolute synchronized server time for this profile map.
	// This keeps dedicated server and clients on the same phase even when clients
	// connect a few seconds after the server process starts.
	PhaseStartWorldTime = 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SW-RIPPLE-PROFILE] Begin Role=%s RippleCount=%d QueriesPerFrame=%d Warmup=%.2f Settle=%.2f Measure=%.2f EventStructBytes=%d AutoQuit=%s"),
		GetRoleName(),
		RippleCount,
		QueriesPerFrame,
		WarmupSeconds,
		ReplicationSettleSeconds,
		MeasurementSeconds,
		static_cast<int32>(sizeof(FSWRippleEvent)),
		bAutoQuit ? TEXT("true") : TEXT("false"));

	TRACE_BOOKMARK(TEXT("SWProfile Ripple Warmup Begin Role=%s R=%d Q=%d"), GetRoleName(), RippleCount, QueriesPerFrame);
}

void ASWRippleProfileController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_ProfileControllerTick);

	const double PhaseElapsed = GetWorldTime() - PhaseStartWorldTime;
	if (!bNetworkSnapshotCaptured && GetWorldTime() >= FMath::Max(0.0f, WarmupSeconds - 1.0f))
	{
		CaptureNetworkSnapshot();
	}
	switch (Phase)
	{
	case EPhase::Warmup:
		if (GetWorldTime() >= WarmupSeconds)
		{
			BeginRippleScenario();
			Phase = EPhase::ReplicationSettle;
			PhaseStartWorldTime = GetWorldTime();
		}
		break;

	case EPhase::ReplicationSettle:
		if (PhaseElapsed >= ReplicationSettleSeconds)
		{
			BeginMeasurement();
			Phase = EPhase::Measure;
			PhaseStartWorldTime = GetWorldTime();
		}
		break;

	case EPhase::Measure:
		RunQueryBatch();
		if (PhaseElapsed >= MeasurementSeconds)
		{
			CompleteMeasurement();
			Phase = EPhase::Complete;
		}
		break;

	case EPhase::Complete:
	default:
		break;
	}
}

void ASWRippleProfileController::CaptureNetworkSnapshot()
{
	bNetworkSnapshotCaptured = true;
	const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	if (!NetDriver)
	{
		return;
	}

	NetworkStartInBytes = NetDriver->InTotalBytes;
	NetworkStartOutBytes = NetDriver->OutTotalBytes;
	NetworkStartInPackets = NetDriver->InTotalPackets;
	NetworkStartOutPackets = NetDriver->OutTotalPackets;
	NetworkStartInBunches = NetDriver->InTotalBunches;
	NetworkStartOutBunches = NetDriver->OutTotalBunches;
	TRACE_BOOKMARK(TEXT("SWProfile Ripple Network Window Begin Role=%s"), GetRoleName());
}

void ASWRippleProfileController::ParseCommandLineOverrides()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("SWProfileRippleCount="), RippleCount);
	FParse::Value(CommandLine, TEXT("SWProfileRippleQueries="), QueriesPerFrame);
	FParse::Value(CommandLine, TEXT("SWProfileWarmup="), WarmupSeconds);
	FParse::Value(CommandLine, TEXT("SWProfileSettle="), ReplicationSettleSeconds);
	FParse::Value(CommandLine, TEXT("SWProfileDuration="), MeasurementSeconds);
	FParse::Value(CommandLine, TEXT("SWProfileSpawnRadius="), RippleSpawnRadius);
	FParse::Value(CommandLine, TEXT("SWProfileQueryExtent="), QueryAreaHalfExtent);
	bAutoQuit = FParse::Param(CommandLine, TEXT("SWProfileAutoQuit"));

	RippleCount = FMath::Clamp(RippleCount, 0, 32);
	QueriesPerFrame = FMath::Max(0, QueriesPerFrame);
	WarmupSeconds = FMath::Max(0.0f, WarmupSeconds);
	ReplicationSettleSeconds = FMath::Max(0.0f, ReplicationSettleSeconds);
	MeasurementSeconds = FMath::Max(0.1f, MeasurementSeconds);
}

void ASWRippleProfileController::BuildQueryPositions()
{
	QueryPositions.Reset(QueriesPerFrame);
	if (QueriesPerFrame <= 0)
	{
		return;
	}

	const int32 SideLength = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(QueriesPerFrame))));
	const FVector Center = GetActorLocation();
	for (int32 Index = 0; Index < QueriesPerFrame; ++Index)
	{
		const int32 XIndex = Index % SideLength;
		const int32 YIndex = Index / SideLength;
		const float XAlpha = SideLength > 1 ? static_cast<float>(XIndex) / static_cast<float>(SideLength - 1) : 0.5f;
		const float YAlpha = SideLength > 1 ? static_cast<float>(YIndex) / static_cast<float>(SideLength - 1) : 0.5f;
		QueryPositions.Add(FVector(
			Center.X + FMath::Lerp(-QueryAreaHalfExtent, QueryAreaHalfExtent, XAlpha),
			Center.Y + FMath::Lerp(-QueryAreaHalfExtent, QueryAreaHalfExtent, YAlpha),
			Center.Z));
	}
}

void ASWRippleProfileController::BeginRippleScenario()
{
	TRACE_BOOKMARK(TEXT("SWProfile Ripple Spawn Begin Role=%s Requested=%d"), GetRoleName(), RippleCount);
	AcceptedRippleCount = 0;

	UWorld* World = GetWorld();
	USWRippleStateSubsystem* StateSubsystem = World ? World->GetSubsystem<USWRippleStateSubsystem>() : nullptr;
	if (World && World->GetNetMode() != NM_Client && StateSubsystem)
	{
		const FVector Center3D = GetActorLocation();
		const FVector2D Center(Center3D.X, Center3D.Y);
		for (int32 Index = 0; Index < RippleCount; ++Index)
		{
			const float Angle = RippleCount > 0 ? (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(RippleCount) : 0.0f;
			const float RingAlpha = static_cast<float>((Index % 4) + 1) / 4.0f;
			const FVector2D Origin = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RippleSpawnRadius * RingAlpha;
			AcceptedRippleCount += StateSubsystem->SubmitAuthoritativeRipple(
				Origin,
				InitialAmplitude,
				WaveSpeed,
				DecayRate,
				WaveLength) ? 1 : 0;
		}
	}

	const int32 StoredEvents = StateSubsystem ? StateSubsystem->GetEventCount() : 0;
	UE_LOG(LogTemp, Display,
		TEXT("[SW-RIPPLE-PROFILE] Spawn Role=%s Requested=%d Accepted=%d StoredEvents=%d"),
		GetRoleName(), RippleCount, AcceptedRippleCount, StoredEvents);
	TRACE_BOOKMARK(TEXT("SWProfile Ripple Spawn End Role=%s Accepted=%d Stored=%d"), GetRoleName(), AcceptedRippleCount, StoredEvents);
}

void ASWRippleProfileController::BeginMeasurement()
{
	MeasurementStartSnapshot = FSWRippleProfile::Capture();
	const USWRippleStateSubsystem* StateSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<USWRippleStateSubsystem>()
		: nullptr;
	const int32 StoredEvents = StateSubsystem ? StateSubsystem->GetEventCount() : 0;

	UE_LOG(LogTemp, Display,
		TEXT("[SW-RIPPLE-PROFILE] MeasureBegin Role=%s R=%d Q=%d StoredEvents=%d Revision=%u"),
		GetRoleName(), RippleCount, QueriesPerFrame, StoredEvents, StateSubsystem ? StateSubsystem->GetRevision() : 0);
	TRACE_BOOKMARK(TEXT("SWProfile Ripple Measure Begin Role=%s R=%d Q=%d"), GetRoleName(), RippleCount, QueriesPerFrame);
}

void ASWRippleProfileController::RunQueryBatch()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_ProfileQueryBatch);
	const uint64 ProfileStartCycles = FPlatformTime::Cycles64();
	UWorld* World = GetWorld();
	const USWRippleStateSubsystem* StateSubsystem = World ? World->GetSubsystem<USWRippleStateSubsystem>() : nullptr;
	if (!StateSubsystem)
	{
		return;
	}

	const double ServerTime = StateSubsystem->GetServerTime();
	float FrameResult = 0.0f;
	for (const FVector& QueryPosition : QueryPositions)
	{
		FrameResult += StateSubsystem->GetRippleHeight(QueryPosition, ServerTime);
	}
	QueryResultSink = FMath::Fmod(QueryResultSink + FrameResult, 100000.0f);
	FSWRippleProfile::RecordQueryBatch(FPlatformTime::Cycles64() - ProfileStartCycles);
}

void ASWRippleProfileController::CompleteMeasurement()
{
	const FSWRippleProfileSnapshot EndSnapshot = FSWRippleProfile::Capture();
	LogMetricDelta(EndSnapshot);
	const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	if (bNetworkSnapshotCaptured && NetDriver)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SW-RIPPLE-PROFILE] NetworkSummary Role=%s R=%d WindowSeconds=%.3f InBytes=%u OutBytes=%u InPackets=%u OutPackets=%u InBunches=%u OutBunches=%u"),
			GetRoleName(),
			RippleCount,
			1.0f + ReplicationSettleSeconds + MeasurementSeconds,
			NetDriver->InTotalBytes - NetworkStartInBytes,
			NetDriver->OutTotalBytes - NetworkStartOutBytes,
			NetDriver->InTotalPackets - NetworkStartInPackets,
			NetDriver->OutTotalPackets - NetworkStartOutPackets,
			NetDriver->InTotalBunches - NetworkStartInBunches,
			NetDriver->OutTotalBunches - NetworkStartOutBunches);
	}
	TRACE_BOOKMARK(TEXT("SWProfile Ripple Measure End Role=%s R=%d Q=%d"), GetRoleName(), RippleCount, QueriesPerFrame);

	if (bAutoQuit)
	{
		UE_LOG(LogTemp, Display, TEXT("[SW-RIPPLE-PROFILE] AutoQuit Role=%s"), GetRoleName());
		FPlatformMisc::RequestExit(false);
	}
}

void ASWRippleProfileController::LogMetricDelta(const FSWRippleProfileSnapshot& EndSnapshot) const
{
	const FSWRippleProfileSnapshot& Start = MeasurementStartSnapshot;
	const int64 Evaluations = Delta(EndSnapshot.EvaluationCalls, Start.EvaluationCalls);
	const int64 Scanned = Delta(EndSnapshot.EventsScanned, Start.EventsScanned);
	const int64 Uploads = Delta(EndSnapshot.TextureUploadEnqueues, Start.TextureUploadEnqueues);
	const int64 UnchangedUploads = Delta(
		EndSnapshot.TextureUpdatesWithoutRevisionChange,
		Start.TextureUpdatesWithoutRevisionChange);
	const int64 QueryBatchCalls = Delta(EndSnapshot.QueryBatchCalls, Start.QueryBatchCalls);
	const int64 QueryBatchCycles = Delta(EndSnapshot.QueryBatchCycles, Start.QueryBatchCycles);
	const double QueryBatchMs = FPlatformTime::ToSeconds64(QueryBatchCycles) * 1000.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SW-RIPPLE-PROFILE] Summary Role=%s R=%d Q=%d Duration=%.3f EvalCalls=%lld EventsScanned=%lld ActiveEvaluated=%lld EnvelopeEval=%lld AvgEventsPerEval=%.3f QueryBatchCalls=%lld QueryBatchMs=%.6f QueryBatchAvgMs=%.9f FullSnapshots=%lld FullSnapshotBytes=%lld FullSnapshotMs=%.6f ActiveSnapshots=%lld ActiveSnapshotBytes=%lld ActiveSnapshotMs=%.6f TextureUpdates=%lld TextureUpdateMs=%.6f TextureUploads=%lld TextureUploadBytes=%lld UnchangedRevisionUploads=%lld MaterialBindPasses=%lld MaterialBindMs=%.6f WaterBodiesVisited=%lld MaterialWrites=%lld AuthAdded=%lld RepApplied=%lld RepRemoved=%lld QuerySink=%.6f"),
		GetRoleName(),
		RippleCount,
		QueriesPerFrame,
		MeasurementSeconds,
		Evaluations,
		Scanned,
		Delta(EndSnapshot.ActiveEventsEvaluated, Start.ActiveEventsEvaluated),
		Delta(EndSnapshot.EnvelopeEvaluations, Start.EnvelopeEvaluations),
		Evaluations > 0 ? static_cast<double>(Scanned) / static_cast<double>(Evaluations) : 0.0,
		QueryBatchCalls,
		QueryBatchMs,
		QueryBatchCalls > 0 ? QueryBatchMs / static_cast<double>(QueryBatchCalls) : 0.0,
		Delta(EndSnapshot.FullSnapshotCalls, Start.FullSnapshotCalls),
		Delta(EndSnapshot.FullSnapshotBytes, Start.FullSnapshotBytes),
		FPlatformTime::ToSeconds64(Delta(EndSnapshot.FullSnapshotCycles, Start.FullSnapshotCycles)) * 1000.0,
		Delta(EndSnapshot.ActiveSnapshotCalls, Start.ActiveSnapshotCalls),
		Delta(EndSnapshot.ActiveSnapshotBytes, Start.ActiveSnapshotBytes),
		FPlatformTime::ToSeconds64(Delta(EndSnapshot.ActiveSnapshotCycles, Start.ActiveSnapshotCycles)) * 1000.0,
		Delta(EndSnapshot.TextureUpdateCalls, Start.TextureUpdateCalls),
		FPlatformTime::ToSeconds64(Delta(EndSnapshot.TextureUpdateCycles, Start.TextureUpdateCycles)) * 1000.0,
		Uploads,
		Delta(EndSnapshot.TextureUploadBytes, Start.TextureUploadBytes),
		UnchangedUploads,
		Delta(EndSnapshot.MaterialBindPasses, Start.MaterialBindPasses),
		FPlatformTime::ToSeconds64(Delta(EndSnapshot.MaterialBindCycles, Start.MaterialBindCycles)) * 1000.0,
		Delta(EndSnapshot.WaterBodiesVisited, Start.WaterBodiesVisited),
		Delta(EndSnapshot.MaterialParameterWrites, Start.MaterialParameterWrites),
		Delta(EndSnapshot.AuthoritativeEventsAdded, Start.AuthoritativeEventsAdded),
		Delta(EndSnapshot.ReplicatedEventsApplied, Start.ReplicatedEventsApplied),
		Delta(EndSnapshot.ReplicatedEventsRemoved, Start.ReplicatedEventsRemoved),
		QueryResultSink);
}

double ASWRippleProfileController::GetWorldTime() const
{
	const UWorld* World = GetWorld();
	const USWRippleStateSubsystem* StateSubsystem = World
		? World->GetSubsystem<USWRippleStateSubsystem>()
		: nullptr;
	return StateSubsystem ? StateSubsystem->GetServerTime() : (World ? static_cast<double>(World->GetTimeSeconds()) : 0.0);
}

const TCHAR* ASWRippleProfileController::GetRoleName() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("NoWorld");
	}

	switch (World->GetNetMode())
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}
