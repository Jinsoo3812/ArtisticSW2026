#include "SWShipWakeSubsystem.h"

#include "SWKelvinWakeAtlas.h"

#include "Async/ParallelFor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSWShipWake, Log, All);

namespace SWShipWakeMaterial
{
	const FName TextureParameter(TEXT("ShipWakeTex"));
	const FName TrajectoryTextureParameter(TEXT("ShipWakeTrajectoryTex"));
	const FName AtlasTextureParameter(TEXT("ShipWakeAtlas"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
	const FName PreviousHeightFieldParameter(TEXT("ShipWakePreviousHeightField"));
	const FName HeightFieldParameter(TEXT("ShipWakeHeightField"));
	const FName PreviousFieldCenterParameter(TEXT("ShipWakePreviousFieldCenter"));
	const FName FieldCenterParameter(TEXT("ShipWakeFieldCenter"));
	const FName FieldSizeParameter(TEXT("ShipWakeFieldSizeCm"));
	const FName HistoryAlphaParameter(TEXT("ShipWakeHistoryAlpha"));
	const FName EnableParameter(TEXT("ShipWakeEnable"));
}

namespace SWShipWakeField
{
	TAutoConsoleVariable<int32> Enable(
		TEXT("sw.ShipWake.Enable"), 1,
		TEXT("Enables M5 Kelvin output for rendering and CPU buoyancy queries."));
	TAutoConsoleVariable<int32> FreezeHistory(
		TEXT("sw.ShipWake.FreezeHistory"), 0,
		TEXT("Freezes M5 field stepping and interpolation for runtime isolation."));
	TAutoConsoleVariable<int32> DebugLog(
		TEXT("sw.ShipWake.DebugLog"), 0,
		TEXT("Logs bounded M5 runtime field, timing, event and interpolation diagnostics."));
	TAutoConsoleVariable<float> DebugLogInterval(
		TEXT("sw.ShipWake.DebugLogInterval"), 0.5f,
		TEXT("Seconds between M5 runtime diagnostic records."));
	TAutoConsoleVariable<float> DebugForceHistoryAlpha(
		TEXT("sw.ShipWake.DebugForceHistoryAlpha"), -1.0f,
		TEXT("-1 uses automatic interpolation; 0 freezes Previous; 1 freezes Current."));
	TAutoConsoleVariable<int32> DebugLockFieldCenter(
		TEXT("sw.ShipWake.DebugLockFieldCenter"), 0,
		TEXT("Locks the field center at its current value while source/history still update."));
	TAutoConsoleVariable<int32> Resolution(
		TEXT("sw.ShipWake.FieldResolution"), 256,
		TEXT("M5 CPU/GPU Golden-history field resolution per axis."));
	TAutoConsoleVariable<float> WorldSizeCm(
		TEXT("sw.ShipWake.FieldWorldSizeCm"), 80000.0f,
		TEXT("M5 Golden-history field width and height in centimeters."));
	TAutoConsoleVariable<float> SimulationHz(
		TEXT("sw.ShipWake.FieldSimulationHz"), 20.0f,
		TEXT("M5 Golden-history fixed update rate."));
	TAutoConsoleVariable<float> StateInterpolationDelay(
		TEXT("sw.ShipWake.StateInterpolationDelay"), 0.10f,
		TEXT("Seconds of emitter sample history used to bracket and interpolate each fixed step."));
	TAutoConsoleVariable<float> HistoryResponseRate(
		TEXT("sw.ShipWake.HistoryResponseRate"), 4.0f,
		TEXT("Rate at which the recorded field accepts newly selected Golden slices."));
	TAutoConsoleVariable<float> HistoryMomentum(
		TEXT("sw.ShipWake.HistoryMomentum"), 0.0f,
		TEXT("Previous/current state momentum. Keep below 0.5 to avoid ringing."));
	TAutoConsoleVariable<float> MaximumHeightCm(
		TEXT("sw.ShipWake.FieldMaximumHeightCm"), 200.0f,
		TEXT("Absolute M5 recorded displacement clamp in centimeters."));
}

namespace
{
	FSWShipWakeEvent InterpolateWakeSample(
		const FSWShipWakeEvent& Previous,
		const FSWShipWakeEvent& Next,
		const double EvaluationServerTime)
	{
		const double SampleDuration = Next.UpdateServerTime - Previous.UpdateServerTime;
		const float Alpha = SampleDuration > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(static_cast<float>(
				(EvaluationServerTime - Previous.UpdateServerTime) / SampleDuration), 0.0f, 1.0f)
			: 0.0f;

		FSWShipWakeEvent Result = Previous;
		Result.Origin = FMath::Lerp(Previous.Origin, Next.Origin, Alpha);
		const FVector2D BlendedForward = FMath::Lerp(Previous.Forward, Next.Forward, Alpha);
		Result.Forward = BlendedForward.IsNearlyZero()
			? Previous.Forward.GetSafeNormal()
			: BlendedForward.GetSafeNormal();
		Result.UpdateServerTime = EvaluationServerTime;
		Result.Amplitude = FMath::Lerp(Previous.Amplitude, Next.Amplitude, Alpha);
		Result.SpeedCmPerSecond = FMath::Lerp(
			Previous.SpeedCmPerSecond, Next.SpeedCmPerSecond, Alpha);
		Result.AdvectionSpeedCmPerSecond = FMath::Lerp(
			Previous.AdvectionSpeedCmPerSecond, Next.AdvectionSpeedCmPerSecond, Alpha);
		Result.PressureSizeCm = FMath::Lerp(Previous.PressureSizeCm, Next.PressureSizeCm, Alpha);
		Result.LongitudinalScale = FMath::Lerp(
			Previous.LongitudinalScale, Next.LongitudinalScale, Alpha);
		Result.LateralScale = FMath::Lerp(Previous.LateralScale, Next.LateralScale, Alpha);
		Result.NearHullSuppressDistanceCm = FMath::Lerp(
			Previous.NearHullSuppressDistanceCm, Next.NearHullSuppressDistanceCm, Alpha);
		Result.HullLengthCm = FMath::Lerp(Previous.HullLengthCm, Next.HullLengthCm, Alpha);
		Result.BeamWidthCm = FMath::Lerp(Previous.BeamWidthCm, Next.BeamWidthCm, Alpha);
		Result.DraftCm = FMath::Lerp(Previous.DraftCm, Next.DraftCm, Alpha);
		Result.WakeLengthCm = FMath::Lerp(Previous.WakeLengthCm, Next.WakeLengthCm, Alpha);
		Result.StateLifetime = FMath::Max(Previous.StateLifetime, Next.StateLifetime);
		Result.TransverseStrength = FMath::Lerp(
			Previous.TransverseStrength, Next.TransverseStrength, Alpha);
		Result.DivergentStrength = FMath::Lerp(
			Previous.DivergentStrength, Next.DivergentStrength, Alpha);
		Result.SternStrength = FMath::Lerp(Previous.SternStrength, Next.SternStrength, Alpha);
		Result.SternPhaseOffsetRadians = FMath::Lerp(
			Previous.SternPhaseOffsetRadians, Next.SternPhaseOffsetRadians, Alpha);

		if (Previous.TrajectoryPoints.Num() == Next.TrajectoryPoints.Num())
		{
			Result.TrajectoryPoints.SetNum(Previous.TrajectoryPoints.Num());
			for (int32 Index = 0; Index < Result.TrajectoryPoints.Num(); ++Index)
			{
				Result.TrajectoryPoints[Index] = FMath::Lerp(
					Previous.TrajectoryPoints[Index], Next.TrajectoryPoints[Index], Alpha);
			}
		}
		if (!Result.TrajectoryPoints.IsEmpty())
		{
			Result.TrajectoryPoints[0] = Result.Origin;
		}
		return Result;
	}

	void ResolveWakeSamplesAtTime(
		TConstArrayView<FSWShipWakeEvent> Samples,
		const double EvaluationServerTime,
		TArray<FSWShipWakeEvent>& OutEvents)
	{
		OutEvents.Reset();
		struct FSampleBracket
		{
			int32 EventId = 0;
			const FSWShipWakeEvent* Previous = nullptr;
			const FSWShipWakeEvent* Next = nullptr;
		};
		TArray<FSampleBracket, TInlineAllocator<USWShipWakeSubsystem::WakeCapacity>> Brackets;
		for (const FSWShipWakeEvent& Sample : Samples)
		{
			FSampleBracket* Bracket = Brackets.FindByPredicate(
				[&Sample](const FSampleBracket& Candidate)
				{
					return Candidate.EventId == Sample.EventId;
				});
			if (!Bracket)
			{
				Bracket = &Brackets.AddDefaulted_GetRef();
				Bracket->EventId = Sample.EventId;
			}
			if (Sample.UpdateServerTime <= EvaluationServerTime)
			{
				if (!Bracket->Previous
					|| Sample.UpdateServerTime > Bracket->Previous->UpdateServerTime)
				{
					Bracket->Previous = &Sample;
				}
			}
			else if (!Bracket->Next
				|| Sample.UpdateServerTime < Bracket->Next->UpdateServerTime)
			{
				Bracket->Next = &Sample;
			}
		}

		OutEvents.Reserve(Brackets.Num());
		for (const FSampleBracket& Bracket : Brackets)
		{
			if (!Bracket.Previous)
			{
				continue;
			}
			if (Bracket.Next
				&& Bracket.Next->UpdateServerTime < Bracket.Previous->GetExpireServerTime())
			{
				OutEvents.Add(InterpolateWakeSample(
					*Bracket.Previous, *Bracket.Next, EvaluationServerTime));
			}
			else if (Bracket.Previous->IsActiveAt(EvaluationServerTime))
			{
				OutEvents.Add(*Bracket.Previous);
			}
		}
	}
}

void USWShipWakeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FSWKelvinWakeAtlas::Get().Initialize();
	InitializeHeightHistory();
	if (IsRunningDedicatedServer())
	{
		return;
	}

	WakeTexture = UTexture2D::CreateTransient(WakeCapacity, 5, PF_A32B32G32R32F, TEXT("SWShipWakeTex"));
	if (WakeTexture)
	{
		WakeTexture->SRGB = false;
		WakeTexture->CompressionSettings = TC_VectorDisplacementmap;
		WakeTexture->Filter = TF_Nearest;
		WakeTexture->AddressX = TA_Clamp;
		WakeTexture->AddressY = TA_Clamp;
		WakeTexture->NeverStream = true;
		WakeTexture->UpdateResource();
	}

	TrajectoryTexture = UTexture2D::CreateTransient(
		TrajectoryCapacity, WakeCapacity, PF_A32B32G32R32F, TEXT("SWShipWakeTrajectoryTex"));
	if (TrajectoryTexture)
	{
		TrajectoryTexture->SRGB = false;
		TrajectoryTexture->CompressionSettings = TC_VectorDisplacementmap;
		TrajectoryTexture->Filter = TF_Nearest;
		TrajectoryTexture->AddressX = TA_Clamp;
		TrajectoryTexture->AddressY = TA_Clamp;
		TrajectoryTexture->NeverStream = true;
		TrajectoryTexture->UpdateResource();
	}
	KelvinAtlasTexture = FSWKelvinWakeAtlas::Get().CreateTransientTexture(TEXT("SWKelvinWakeAtlasR16F"));
}

void USWShipWakeSubsystem::Deinitialize()
{
	{
		FWriteScopeLock Lock(EventsLock);
		Events.Reset();
	}
	WakeTexture = nullptr;
	TrajectoryTexture = nullptr;
	KelvinAtlasTexture = nullptr;
	HeightHistoryTextures.Reset();
	HeightHistoryValues.Reset();
	HeightHistoryCenters.Reset();
	bHeightFieldInitialized = false;
	Super::Deinitialize();
}

TStatId USWShipWakeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWShipWakeSubsystem, STATGROUP_Tickables);
}

void USWShipWakeSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld())
	{
		return;
	}

	const double ServerTime = GetServerTime();
	RemoveExpiredEvents(ServerTime);
	if (!bHeightFieldInitialized)
	{
		InitializeHeightHistory();
	}
	const float SimulationHz = FMath::Clamp(
		SWShipWakeField::SimulationHz.GetValueOnGameThread(), 1.0f, 60.0f);
	const float FixedDeltaTime = 1.0f / SimulationHz;
	const double InterpolationDelay = FMath::Max(
		static_cast<double>(SWShipWakeField::StateInterpolationDelay.GetValueOnGameThread()),
		static_cast<double>(FixedDeltaTime));
	const double RenderEvaluationTime = ServerTime - InterpolationDelay;
	if (SWShipWakeField::FreezeHistory.GetValueOnGameThread() == 0)
	{
		HeightFieldAccumulator = FMath::Min(HeightFieldAccumulator + DeltaTime, FixedDeltaTime * 3.0f);
		int32 StepCount = 0;
		while (HeightFieldAccumulator >= FixedDeltaTime && StepCount < 2)
		{
			StepHeightHistory(
				ServerTime - InterpolationDelay - HeightFieldAccumulator + FixedDeltaTime,
				FixedDeltaTime);
			HeightFieldAccumulator -= FixedDeltaTime;
			++StepCount;
		}
		{
			FWriteScopeLock Lock(HeightHistoryLock);
			HeightHistoryInterpolationAlpha = FMath::Clamp(
				HeightFieldAccumulator / FixedDeltaTime, 0.0f, 1.0f);
		}
	}
	if (!IsRunningDedicatedServer())
	{
		UpdateTexture(RenderEvaluationTime);
		BindToWaterMaterials(RenderEvaluationTime);
	}
	LogRuntimeDiagnostics(ServerTime, DeltaTime);
}

void USWShipWakeSubsystem::AddOrUpdateEvent(const FSWShipWakeEvent& Event)
{
	if (Event.EventId == 0 || Event.Amplitude <= 0.0f || Event.StateLifetime <= 0.0f)
	{
		return;
	}

	FWriteScopeLock Lock(EventsLock);
	if (FSWShipWakeEvent* Duplicate = Events.FindByPredicate(
		[&Event](const FSWShipWakeEvent& Candidate)
		{
			return Candidate.EventId == Event.EventId
				&& FMath::IsNearlyEqual(Candidate.UpdateServerTime, Event.UpdateServerTime, 1.e-6);
		}))
	{
		*Duplicate = Event;
		return;
	}

	while (Events.Num() >= WakeSampleCapacity)
	{
		int32 OldestIndex = 0;
		for (int32 Index = 1; Index < Events.Num(); ++Index)
		{
			if (Events[Index].UpdateServerTime < Events[OldestIndex].UpdateServerTime)
			{
				OldestIndex = Index;
			}
		}
		Events.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
	}
	Events.Add(Event);
}

void USWShipWakeSubsystem::GetEventsSnapshot(TArray<FSWShipWakeEvent>& OutEvents) const
{
	FReadScopeLock Lock(EventsLock);
	OutEvents = Events;
}

void USWShipWakeSubsystem::GetActiveEventsSnapshot(
	const double ServerTime,
	TArray<FSWShipWakeEvent>& OutEvents) const
{
	FReadScopeLock Lock(EventsLock);
	ResolveWakeSamplesAtTime(Events, ServerTime, OutEvents);
}

float USWShipWakeSubsystem::GetWakeHeight(const FVector& WorldPosition, const double ServerTime) const
{
	if (SWShipWakeField::Enable.GetValueOnAnyThread() == 0)
	{
		return 0.0f;
	}
	if (bHeightFieldInitialized)
	{
		return SampleHeightHistory(FVector2D(WorldPosition));
	}
	TArray<FSWShipWakeEvent> ActiveEvents;
	GetActiveEventsSnapshot(ServerTime, ActiveEvents);
	return FSWShipWakeEvaluator::EvaluateHeight(FVector2D(WorldPosition), ServerTime, ActiveEvents);
}

FVector2D USWShipWakeSubsystem::GetWakeGradient(const FVector& WorldPosition, const double ServerTime) const
{
	if (SWShipWakeField::Enable.GetValueOnAnyThread() == 0)
	{
		return FVector2D::ZeroVector;
	}
	if (bHeightFieldInitialized)
	{
		const float SampleDistance = FMath::Max(HeightFieldWorldSizeCm / FMath::Max(HeightFieldResolution, 1), 1.0f);
		const FVector2D Position(WorldPosition);
		return FVector2D(
			(SampleHeightHistory(Position + FVector2D(SampleDistance, 0.0))
				- SampleHeightHistory(Position - FVector2D(SampleDistance, 0.0))) / (2.0f * SampleDistance),
			(SampleHeightHistory(Position + FVector2D(0.0, SampleDistance))
				- SampleHeightHistory(Position - FVector2D(0.0, SampleDistance))) / (2.0f * SampleDistance));
	}
	TArray<FSWShipWakeEvent> ActiveEvents;
	GetActiveEventsSnapshot(ServerTime, ActiveEvents);
	return FSWShipWakeEvaluator::EvaluateGradient(FVector2D(WorldPosition), ServerTime, ActiveEvents);
}

double USWShipWakeSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

int32 USWShipWakeSubsystem::GetEventCount() const
{
	FReadScopeLock Lock(EventsLock);
	return Events.Num();
}

void USWShipWakeSubsystem::RemoveExpiredEvents(const double ServerTime)
{
	FWriteScopeLock Lock(EventsLock);
	Events.RemoveAllSwap(
		[ServerTime, Retention = PhysicsHistoryRetentionSeconds](const FSWShipWakeEvent& Event)
		{
			return ServerTime >= Event.GetExpireServerTime() + Retention;
		},
		EAllowShrinking::No);
}

void USWShipWakeSubsystem::UpdateTexture(const double ServerTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_ShipWake_UpdateTexture);
	if (!WakeTexture || !TrajectoryTexture)
	{
		return;
	}

	TArray<FSWShipWakeEvent> ActiveEvents;
	GetActiveEventsSnapshot(ServerTime, ActiveEvents);
	ActiveEvents.Sort([](const FSWShipWakeEvent& A, const FSWShipWakeEvent& B)
	{
		return A.UpdateServerTime < B.UpdateServerTime;
	});
	const int32 Count = FMath::Min(ActiveEvents.Num(), WakeCapacity);

	TArray<FLinearColor> Pixels;
	Pixels.SetNumZeroed(WakeCapacity * 5);
	TArray<FLinearColor> TrajectoryPixels;
	TrajectoryPixels.SetNumZeroed(WakeCapacity * TrajectoryCapacity);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWShipWakeEvent& Event = ActiveEvents[Index];
		Pixels[Index] = FLinearColor(
			Event.Origin.X,
			Event.Origin.Y,
			static_cast<float>(Event.UpdateServerTime),
			Event.Amplitude);
		Pixels[Index + WakeCapacity] = FLinearColor(
			Event.Forward.X,
			Event.Forward.Y,
			Event.SpeedCmPerSecond,
			Event.PressureSizeCm);
		Pixels[Index + WakeCapacity * 2] = FLinearColor(
			Event.AdvectionSpeedCmPerSecond,
			Event.StateLifetime,
			Event.LongitudinalScale,
			Event.LateralScale);
		Pixels[Index + WakeCapacity * 3] = FLinearColor(
			Event.NearHullSuppressDistanceCm,
			static_cast<float>(FMath::Min(Event.TrajectoryPoints.Num(), TrajectoryCapacity)),
			Event.HullLengthCm,
			0.0f);
		for (int32 PointIndex = 0;
			PointIndex < Event.TrajectoryPoints.Num() && PointIndex < TrajectoryCapacity;
			++PointIndex)
		{
			const FVector2D& Point = Event.TrajectoryPoints[PointIndex];
			TrajectoryPixels[Index * TrajectoryCapacity + PointIndex] = FLinearColor(
				Point.X, Point.Y, 0.0f, 1.0f);
		}
	}

	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(WakeTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeTexture)(
			[TextureResource, Data = MoveTemp(Pixels)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, USWShipWakeSubsystem::WakeCapacity, 5);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					USWShipWakeSubsystem::WakeCapacity * sizeof(FLinearColor),
					reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(TrajectoryTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeTrajectoryTexture)(
			[TextureResource, Data = MoveTemp(TrajectoryPixels)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(
					0, 0, 0, 0,
					USWShipWakeSubsystem::TrajectoryCapacity,
					USWShipWakeSubsystem::WakeCapacity);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					USWShipWakeSubsystem::TrajectoryCapacity * sizeof(FLinearColor),
					reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
	LastUploadedCount = Count;
}

namespace
{
	float SampleHeightState(
		const TArray<float>& Values,
		const int32 Resolution,
		const float WorldSizeCm,
		const FVector2D& Center,
		const FVector2D& WorldPosition)
	{
		if (Resolution < 2 || Values.Num() != Resolution * Resolution || WorldSizeCm <= 1.0f)
		{
			return 0.0f;
		}
		const FVector2D UV = (WorldPosition - Center) / WorldSizeCm + FVector2D(0.5, 0.5);
		if (UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0)
		{
			return 0.0f;
		}
		const float X = FMath::Clamp(static_cast<float>(UV.X) * Resolution - 0.5f, 0.0f, Resolution - 1.0f);
		const float Y = FMath::Clamp(static_cast<float>(UV.Y) * Resolution - 0.5f, 0.0f, Resolution - 1.0f);
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
		const float A = FMath::Lerp(Values[Y0 * Resolution + X0], Values[Y0 * Resolution + X1], X - X0);
		const float B = FMath::Lerp(Values[Y1 * Resolution + X0], Values[Y1 * Resolution + X1], X - X0);
		return FMath::Lerp(A, B, Y - Y0);
	}
}

bool USWShipWakeSubsystem::InitializeHeightHistory()
{
	if (!GetWorld())
	{
		return false;
	}
	const int32 RequestedResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 64, 512);
	const float RequestedWorldSize = FMath::Max(
		SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f);
	if (bHeightFieldInitialized
		&& HeightFieldResolution == RequestedResolution
		&& FMath::IsNearlyEqual(HeightFieldWorldSizeCm, RequestedWorldSize))
	{
		return true;
	}

	FWriteScopeLock Lock(HeightHistoryLock);
	HeightFieldResolution = RequestedResolution;
	HeightFieldWorldSizeCm = RequestedWorldSize;
	HeightHistoryValues.SetNum(3);
	HeightHistoryCenters.SetNumZeroed(3);
	for (TArray<float>& State : HeightHistoryValues)
	{
		State.SetNumZeroed(HeightFieldResolution * HeightFieldResolution);
	}
	PreviousHeightStateIndex = 0;
	CurrentHeightStateIndex = 1;
	NextHeightStateIndex = 2;
	HeightFieldAccumulator = 0.0f;
	HeightHistoryInterpolationAlpha = 0.0f;
	bHeightFieldInitialized = true;
	const FVector2D InitialCenter = ResolveDesiredFieldCenter(GetServerTime());
	for (FVector2D& Center : HeightHistoryCenters)
	{
		Center = InitialCenter;
	}

	HeightHistoryTextures.Reset();
	if (!IsRunningDedicatedServer())
	{
		HeightHistoryTextures.SetNum(3);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UTexture2D* Texture = UTexture2D::CreateTransient(
				HeightFieldResolution,
				HeightFieldResolution,
				PF_R32_FLOAT,
				*FString::Printf(TEXT("SWShipWakeM5History%d"), Index));
			if (!Texture)
			{
				HeightHistoryTextures.Reset();
				break;
			}
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_VectorDisplacementmap;
			Texture->Filter = TF_Bilinear;
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
			Texture->NeverStream = true;
			Texture->UpdateResource();
			HeightHistoryTextures[Index] = Texture;
		}
		for (int32 Index = 0; Index < HeightHistoryTextures.Num(); ++Index)
		{
			UploadHeightHistoryState(Index);
		}
	}

	UE_LOG(LogSWShipWake, Display,
		TEXT("M5 Golden history initialized: 3 states, Resolution=%d SizeCm=%.0f AtlasSlices=%d"),
		HeightFieldResolution, HeightFieldWorldSizeCm, FSWKelvinWakeAtlas::NumSlices);
	return true;
}

void USWShipWakeSubsystem::ResetHeightHistory()
{
	const FVector2D Center = ResolveDesiredFieldCenter(GetServerTime());
	FWriteScopeLock Lock(HeightHistoryLock);
	for (TArray<float>& State : HeightHistoryValues)
	{
		FMemory::Memzero(State.GetData(), State.Num() * sizeof(float));
	}
	for (FVector2D& StateCenter : HeightHistoryCenters)
	{
		StateCenter = Center;
	}
}

FVector2D USWShipWakeSubsystem::ResolveDesiredFieldCenter(const double ServerTime) const
{
	const float FieldSize = FMath::Max(SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f);
	const int32 FieldResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 128, 1024);
	const float TexelWorldSize = FieldSize / static_cast<float>(FieldResolution);
	if (SWShipWakeField::DebugLockFieldCenter.GetValueOnAnyThread() != 0
		&& HeightHistoryCenters.IsValidIndex(CurrentHeightStateIndex))
	{
		return HeightHistoryCenters[CurrentHeightStateIndex];
	}

	TArray<FSWShipWakeEvent> ActiveEvents;
	GetActiveEventsSnapshot(ServerTime, ActiveEvents);
	const FSWShipWakeEvent* NewestEvent = nullptr;
	for (const FSWShipWakeEvent& Event : ActiveEvents)
	{
		if (!NewestEvent || Event.UpdateServerTime > NewestEvent->UpdateServerTime)
		{
			NewestEvent = &Event;
		}
	}
	if (!NewestEvent)
	{
		return HeightHistoryCenters.IsValidIndex(CurrentHeightStateIndex)
			? HeightHistoryCenters[CurrentHeightStateIndex]
			: FVector2D::ZeroVector;
	}

	const FVector2D Forward = NewestEvent->Forward.IsNearlyZero()
		? FVector2D(1.0, 0.0)
		: NewestEvent->Forward.GetSafeNormal();
	const float Age = static_cast<float>(ServerTime - NewestEvent->UpdateServerTime);
	const FVector2D PredictedApex = NewestEvent->Origin
		+ Forward * NewestEvent->AdvectionSpeedCmPerSecond * FMath::Clamp(Age, 0.0f, 0.20f);
	const float ForwardMargin = FMath::Clamp(
		FMath::Max(NewestEvent->HullLengthCm * 1.5f, FieldSize * 0.08f),
		FieldSize * 0.05f,
		FieldSize * 0.35f);
	const FVector2D FieldCenter = PredictedApex - Forward * (FieldSize * 0.5f - ForwardMargin);
	return FVector2D(
		FMath::GridSnap(FieldCenter.X, TexelWorldSize),
		FMath::GridSnap(FieldCenter.Y, TexelWorldSize));
}

void USWShipWakeSubsystem::StepHeightHistory(const double ServerTime, const float DeltaTime)
{
	const double StepStartSeconds = FPlatformTime::Seconds();
	const int32 RequestedResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 64, 512);
	const float RequestedWorldSize = FMath::Max(
		SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f);
	if (!bHeightFieldInitialized
		|| RequestedResolution != HeightFieldResolution
		|| !FMath::IsNearlyEqual(RequestedWorldSize, HeightFieldWorldSizeCm))
	{
		InitializeHeightHistory();
	}
	if (!bHeightFieldInitialized || HeightHistoryValues.Num() != 3)
	{
		return;
	}

	TArray<FSWShipWakeEvent> EventSnapshot;
	GetActiveEventsSnapshot(ServerTime, EventSnapshot);
	const FVector2D OutputCenter = ResolveDesiredFieldCenter(ServerTime);
	FWriteScopeLock Lock(HeightHistoryLock);
	if (FVector2D::Distance(OutputCenter, HeightHistoryCenters[CurrentHeightStateIndex])
		> HeightFieldWorldSizeCm * 0.45f)
	{
		for (int32 Index = 0; Index < HeightHistoryValues.Num(); ++Index)
		{
			FMemory::Memzero(
				HeightHistoryValues[Index].GetData(),
				HeightHistoryValues[Index].Num() * sizeof(float));
			HeightHistoryCenters[Index] = OutputCenter;
		}
	}

	const TArray<float>& PreviousState = HeightHistoryValues[PreviousHeightStateIndex];
	const TArray<float>& CurrentState = HeightHistoryValues[CurrentHeightStateIndex];
	TArray<float>& NextState = HeightHistoryValues[NextHeightStateIndex];
	const FVector2D PreviousCenter = HeightHistoryCenters[PreviousHeightStateIndex];
	const FVector2D CurrentCenter = HeightHistoryCenters[CurrentHeightStateIndex];
	const float ResponseAlpha = 1.0f - FMath::Exp(
		-FMath::Max(SWShipWakeField::HistoryResponseRate.GetValueOnGameThread(), 0.0f) * DeltaTime);
	const float Momentum = FMath::Clamp(
		SWShipWakeField::HistoryMomentum.GetValueOnGameThread(), 0.0f, 0.49f);
	const float MaximumHeight = FMath::Max(
		SWShipWakeField::MaximumHeightCm.GetValueOnGameThread(), 1.0f);
	const int32 Resolution = HeightFieldResolution;
	const float WorldSize = HeightFieldWorldSizeCm;
	struct FRowStats
	{
		float TargetMinimum = TNumericLimits<float>::Max();
		float TargetMaximum = TNumericLimits<float>::Lowest();
		float OutputMinimum = TNumericLimits<float>::Max();
		float OutputMaximum = TNumericLimits<float>::Lowest();
		float MaximumStepDelta = 0.0f;
		double TargetSumSquares = 0.0;
		double OutputSumSquares = 0.0;
		int32 SaturatedCount = 0;
	};
	TArray<FRowStats> RowStats;
	RowStats.SetNum(Resolution);

	ParallelFor(Resolution, [&](const int32 Y)
	{
		FRowStats& Stats = RowStats[Y];
		for (int32 X = 0; X < Resolution; ++X)
		{
			const FVector2D UV(
				(static_cast<double>(X) + 0.5) / Resolution,
				(static_cast<double>(Y) + 0.5) / Resolution);
			const FVector2D WorldPosition = OutputCenter + (UV - FVector2D(0.5, 0.5)) * WorldSize;
			const float PreviousHeight = SampleHeightState(
				PreviousState, Resolution, WorldSize, PreviousCenter, WorldPosition);
			const float CurrentHeight = SampleHeightState(
				CurrentState, Resolution, WorldSize, CurrentCenter, WorldPosition);
			const float GoldenTarget = FSWShipWakeEvaluator::EvaluateHeight(
				WorldPosition, ServerTime, EventSnapshot);
			const float RecordedHeight = CurrentHeight
				+ (CurrentHeight - PreviousHeight) * Momentum
				+ (GoldenTarget - CurrentHeight) * ResponseAlpha;
			NextState[Y * Resolution + X] = FMath::Clamp(
				RecordedHeight, -MaximumHeight, MaximumHeight);
			const float OutputHeight = NextState[Y * Resolution + X];
			Stats.TargetMinimum = FMath::Min(Stats.TargetMinimum, GoldenTarget);
			Stats.TargetMaximum = FMath::Max(Stats.TargetMaximum, GoldenTarget);
			Stats.OutputMinimum = FMath::Min(Stats.OutputMinimum, OutputHeight);
			Stats.OutputMaximum = FMath::Max(Stats.OutputMaximum, OutputHeight);
			Stats.MaximumStepDelta = FMath::Max(
				Stats.MaximumStepDelta, FMath::Abs(OutputHeight - CurrentHeight));
			Stats.TargetSumSquares += static_cast<double>(GoldenTarget) * GoldenTarget;
			Stats.OutputSumSquares += static_cast<double>(OutputHeight) * OutputHeight;
			Stats.SaturatedCount += FMath::Abs(OutputHeight) >= MaximumHeight - 0.01f ? 1 : 0;
		}
	});

	RuntimeStats.TargetMinimum = TNumericLimits<float>::Max();
	RuntimeStats.TargetMaximum = TNumericLimits<float>::Lowest();
	RuntimeStats.OutputMinimum = TNumericLimits<float>::Max();
	RuntimeStats.OutputMaximum = TNumericLimits<float>::Lowest();
	RuntimeStats.MaximumStepDelta = 0.0f;
	double TargetSumSquares = 0.0;
	double OutputSumSquares = 0.0;
	int32 SaturatedCount = 0;
	for (const FRowStats& Stats : RowStats)
	{
		RuntimeStats.TargetMinimum = FMath::Min(RuntimeStats.TargetMinimum, Stats.TargetMinimum);
		RuntimeStats.TargetMaximum = FMath::Max(RuntimeStats.TargetMaximum, Stats.TargetMaximum);
		RuntimeStats.OutputMinimum = FMath::Min(RuntimeStats.OutputMinimum, Stats.OutputMinimum);
		RuntimeStats.OutputMaximum = FMath::Max(RuntimeStats.OutputMaximum, Stats.OutputMaximum);
		RuntimeStats.MaximumStepDelta = FMath::Max(RuntimeStats.MaximumStepDelta, Stats.MaximumStepDelta);
		TargetSumSquares += Stats.TargetSumSquares;
		OutputSumSquares += Stats.OutputSumSquares;
		SaturatedCount += Stats.SaturatedCount;
	}
	const int32 TexelCount = Resolution * Resolution;
	RuntimeStats.TargetRms = FMath::Sqrt(static_cast<float>(TargetSumSquares / FMath::Max(TexelCount, 1)));
	RuntimeStats.OutputRms = FMath::Sqrt(static_cast<float>(OutputSumSquares / FMath::Max(TexelCount, 1)));
	RuntimeStats.SaturatedFraction = static_cast<float>(SaturatedCount) / FMath::Max(TexelCount, 1);
	RuntimeStats.CenterDelta = OutputCenter - CurrentCenter;
	RuntimeStats.ActiveEventCount = EventSnapshot.Num();
	RuntimeStats.EvaluationServerTime = ServerTime;

	HeightHistoryCenters[NextHeightStateIndex] = OutputCenter;
	UploadHeightHistoryState(NextHeightStateIndex);
	const int32 RecycledStateIndex = PreviousHeightStateIndex;
	PreviousHeightStateIndex = CurrentHeightStateIndex;
	CurrentHeightStateIndex = NextHeightStateIndex;
	NextHeightStateIndex = RecycledStateIndex;
	RuntimeStats.StepMilliseconds = (FPlatformTime::Seconds() - StepStartSeconds) * 1000.0;
}

void USWShipWakeSubsystem::UploadHeightHistoryState(const int32 StateIndex)
{
	if (!HeightHistoryTextures.IsValidIndex(StateIndex)
		|| !HeightHistoryTextures[StateIndex]
		|| !HeightHistoryValues.IsValidIndex(StateIndex))
	{
		return;
	}
	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(
		HeightHistoryTextures[StateIndex]->GetResource()))
	{
		TArray<float> Data = HeightHistoryValues[StateIndex];
		const int32 Resolution = HeightFieldResolution;
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeM5HistoryTexture)(
			[TextureResource, Resolution, Data = MoveTemp(Data)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, Resolution, Resolution);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					Resolution * sizeof(float),
					reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
}

float USWShipWakeSubsystem::SampleHeightHistory(const FVector2D& WorldPosition) const
{
	FReadScopeLock Lock(HeightHistoryLock);
	if (!HeightHistoryValues.IsValidIndex(PreviousHeightStateIndex)
		|| !HeightHistoryValues.IsValidIndex(CurrentHeightStateIndex)
		|| !HeightHistoryCenters.IsValidIndex(PreviousHeightStateIndex)
		|| !HeightHistoryCenters.IsValidIndex(CurrentHeightStateIndex))
	{
		return 0.0f;
	}
	const float PreviousHeight = SampleHeightState(
		HeightHistoryValues[PreviousHeightStateIndex],
		HeightFieldResolution,
		HeightFieldWorldSizeCm,
		HeightHistoryCenters[PreviousHeightStateIndex],
		WorldPosition);
	const float CurrentHeight = SampleHeightState(
		HeightHistoryValues[CurrentHeightStateIndex],
		HeightFieldResolution,
		HeightFieldWorldSizeCm,
		HeightHistoryCenters[CurrentHeightStateIndex],
		WorldPosition);
	return FMath::Lerp(PreviousHeight, CurrentHeight, GetEffectiveHistoryAlpha());
}

float USWShipWakeSubsystem::GetEffectiveHistoryAlpha() const
{
	const float ForcedAlpha = SWShipWakeField::DebugForceHistoryAlpha.GetValueOnAnyThread();
	return ForcedAlpha >= 0.0f
		? FMath::Clamp(ForcedAlpha, 0.0f, 1.0f)
		: FMath::Clamp(HeightHistoryInterpolationAlpha, 0.0f, 1.0f);
}

UTexture2D* USWShipWakeSubsystem::GetHeightField() const
{
	return bHeightFieldInitialized && HeightHistoryTextures.IsValidIndex(CurrentHeightStateIndex)
		? HeightHistoryTextures[CurrentHeightStateIndex]
		: nullptr;
}

UTexture2D* USWShipWakeSubsystem::GetPreviousHeightField() const
{
	return bHeightFieldInitialized && HeightHistoryTextures.IsValidIndex(PreviousHeightStateIndex)
		? HeightHistoryTextures[PreviousHeightStateIndex]
		: nullptr;
}

void USWShipWakeSubsystem::BindToWaterMaterials(const double ServerTime)
{
	UTexture2D* PreviousHeightField = GetPreviousHeightField();
	UTexture2D* CurrentHeightField = GetHeightField();
	if (!GetWorld() || !WakeTexture || !TrajectoryTexture || !KelvinAtlasTexture
		|| !PreviousHeightField || !CurrentHeightField)
	{
		return;
	}
	FVector2D PreviousFieldCenter = FVector2D::ZeroVector;
	FVector2D CurrentFieldCenter = FVector2D::ZeroVector;
	float InterpolationAlpha = 0.0f;
	{
		FReadScopeLock Lock(HeightHistoryLock);
		if (HeightHistoryCenters.IsValidIndex(PreviousHeightStateIndex))
		{
			PreviousFieldCenter = HeightHistoryCenters[PreviousHeightStateIndex];
		}
		if (HeightHistoryCenters.IsValidIndex(CurrentHeightStateIndex))
		{
			CurrentFieldCenter = HeightHistoryCenters[CurrentHeightStateIndex];
		}
		InterpolationAlpha = GetEffectiveHistoryAlpha();
	}

	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		UMaterialInstanceDynamic* WaterMID = Component ? Component->GetWaterMaterialInstance() : nullptr;
		if (!WaterMID)
		{
			continue;
		}
		WaterMID->SetTextureParameterValue(SWShipWakeMaterial::TextureParameter, WakeTexture);
		WaterMID->SetTextureParameterValue(
			SWShipWakeMaterial::TrajectoryTextureParameter, TrajectoryTexture);
		WaterMID->SetTextureParameterValue(
			SWShipWakeMaterial::AtlasTextureParameter, KelvinAtlasTexture);
		WaterMID->SetTextureParameterValue(
			SWShipWakeMaterial::PreviousHeightFieldParameter, PreviousHeightField);
		WaterMID->SetTextureParameterValue(
			SWShipWakeMaterial::HeightFieldParameter, CurrentHeightField);
		WaterMID->SetVectorParameterValue(
			SWShipWakeMaterial::PreviousFieldCenterParameter,
			FLinearColor(PreviousFieldCenter.X, PreviousFieldCenter.Y, 0.0f, 0.0f));
		WaterMID->SetVectorParameterValue(
			SWShipWakeMaterial::FieldCenterParameter,
			FLinearColor(CurrentFieldCenter.X, CurrentFieldCenter.Y, 0.0f, 0.0f));
		WaterMID->SetScalarParameterValue(
			SWShipWakeMaterial::FieldSizeParameter, HeightFieldWorldSizeCm);
		WaterMID->SetScalarParameterValue(
			SWShipWakeMaterial::HistoryAlphaParameter, InterpolationAlpha);
		WaterMID->SetScalarParameterValue(
			SWShipWakeMaterial::EnableParameter,
			SWShipWakeField::Enable.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::CountParameter, static_cast<float>(LastUploadedCount));
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::TimeParameter, static_cast<float>(ServerTime));
	}
}

void USWShipWakeSubsystem::LogRuntimeDiagnostics(
	const double ServerTime,
	const float FrameDeltaTime)
{
	if (SWShipWakeField::DebugLog.GetValueOnGameThread() == 0 || !GetWorld())
	{
		return;
	}
	const float LogInterval = FMath::Max(
		SWShipWakeField::DebugLogInterval.GetValueOnGameThread(), 0.05f);
	if (ServerTime - LastRuntimeLogServerTime < LogInterval)
	{
		return;
	}
	LastRuntimeLogServerTime = ServerTime;

	FHeightHistoryRuntimeStats Stats;
	FVector2D PreviousCenter = FVector2D::ZeroVector;
	FVector2D CurrentCenter = FVector2D::ZeroVector;
	int32 PreviousIndex = 0;
	int32 CurrentIndex = 0;
	int32 NextIndex = 0;
	float AutomaticAlpha = 0.0f;
	float EffectiveAlpha = 0.0f;
	{
		FReadScopeLock Lock(HeightHistoryLock);
		Stats = RuntimeStats;
		PreviousIndex = PreviousHeightStateIndex;
		CurrentIndex = CurrentHeightStateIndex;
		NextIndex = NextHeightStateIndex;
		if (HeightHistoryCenters.IsValidIndex(PreviousIndex))
		{
			PreviousCenter = HeightHistoryCenters[PreviousIndex];
		}
		if (HeightHistoryCenters.IsValidIndex(CurrentIndex))
		{
			CurrentCenter = HeightHistoryCenters[CurrentIndex];
		}
		AutomaticAlpha = HeightHistoryInterpolationAlpha;
		EffectiveAlpha = GetEffectiveHistoryAlpha();
	}

	const double EvaluationServerTime = Stats.EvaluationServerTime;
	TArray<FSWShipWakeEvent> EventSnapshot;
	GetActiveEventsSnapshot(EvaluationServerTime, EventSnapshot);
	const FSWShipWakeEvent* NewestEvent = nullptr;
	for (const FSWShipWakeEvent& Event : EventSnapshot)
	{
		if (!NewestEvent || Event.UpdateServerTime > NewestEvent->UpdateServerTime)
		{
			NewestEvent = &Event;
		}
	}
	const float EventAge = NewestEvent
		? static_cast<float>(EvaluationServerTime - NewestEvent->UpdateServerTime)
		: -1.0f;
	const float EventAmplitude = NewestEvent ? NewestEvent->Amplitude : 0.0f;
	const float EventSpeed = NewestEvent ? NewestEvent->SpeedCmPerSecond : 0.0f;
	const FVector2D EventOrigin = NewestEvent ? NewestEvent->Origin : FVector2D::ZeroVector;
	int32 RawEventCount = 0;
	int32 FutureSampleCount = 0;
	double NewestSampleSignedAge = -1.0;
	{
		FReadScopeLock Lock(EventsLock);
		RawEventCount = Events.Num();
		const FSWShipWakeEvent* NewestRawEvent = nullptr;
		for (const FSWShipWakeEvent& Event : Events)
		{
			FutureSampleCount += Event.UpdateServerTime > EvaluationServerTime ? 1 : 0;
			if (!NewestRawEvent || Event.UpdateServerTime > NewestRawEvent->UpdateServerTime)
			{
				NewestRawEvent = &Event;
			}
		}
		if (NewestRawEvent)
		{
			NewestSampleSignedAge = EvaluationServerTime - NewestRawEvent->UpdateServerTime;
		}
	}

	UE_LOG(LogSWShipWake, Warning,
		TEXT("[M5Runtime] World=%s NetMode=%d Enable=%d Freeze=%d LockCenter=%d FrameMs=%.3f StepMs=%.3f Hz=%.1f EvalLagMs=%.1f AlphaAuto=%.3f AlphaUsed=%.3f Idx=%d/%d/%d PrevCenter=(%.1f,%.1f) CurrCenter=(%.1f,%.1f) CenterStep=(%.1f,%.1f) Events=%d StepEvents=%d TimelineSamples=%d FutureSamples=%d EventAge=%.3f NewestSampleAge=%.3f Amp=%.1f Speed=%.1f Origin=(%.1f,%.1f) Target=[%.2f,%.2f] TargetRMS=%.3f Output=[%.2f,%.2f] OutputRMS=%.3f MaxDelta=%.3f Saturated=%.2f%%"),
		*GetWorld()->GetName(),
		static_cast<int32>(GetWorld()->GetNetMode()),
		SWShipWakeField::Enable.GetValueOnGameThread(),
		SWShipWakeField::FreezeHistory.GetValueOnGameThread(),
		SWShipWakeField::DebugLockFieldCenter.GetValueOnGameThread(),
		FrameDeltaTime * 1000.0f,
		Stats.StepMilliseconds,
		SWShipWakeField::SimulationHz.GetValueOnGameThread(),
		static_cast<float>((ServerTime - EvaluationServerTime) * 1000.0),
		AutomaticAlpha,
		EffectiveAlpha,
		PreviousIndex,
		CurrentIndex,
		NextIndex,
		PreviousCenter.X, PreviousCenter.Y,
		CurrentCenter.X, CurrentCenter.Y,
		Stats.CenterDelta.X, Stats.CenterDelta.Y,
		EventSnapshot.Num(),
		Stats.ActiveEventCount,
		RawEventCount,
		FutureSampleCount,
		EventAge,
		NewestSampleSignedAge,
		EventAmplitude,
		EventSpeed,
		EventOrigin.X, EventOrigin.Y,
		Stats.TargetMinimum, Stats.TargetMaximum,
		Stats.TargetRms,
		Stats.OutputMinimum, Stats.OutputMaximum,
		Stats.OutputRms,
		Stats.MaximumStepDelta,
		Stats.SaturatedFraction * 100.0f);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWShipWakeTimelineInterpolationTest,
	"ArtisticSW.Water.ShipWake.M5TimelineInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeTimelineInterpolationTest::RunTest(const FString& Parameters)
{
	FSWShipWakeEvent Previous;
	Previous.EventId = 17;
	Previous.Origin = FVector2D(100.0, 200.0);
	Previous.Forward = FVector2D(1.0, 0.0);
	Previous.TrajectoryPoints = { Previous.Origin, FVector2D(0.0, 200.0) };
	Previous.UpdateServerTime = 10.0;
	Previous.Amplitude = 100.0f;
	Previous.SpeedCmPerSecond = 400.0f;
	Previous.AdvectionSpeedCmPerSecond = 500.0f;
	Previous.PressureSizeCm = 2400.0f;
	Previous.StateLifetime = 1.0f;

	FSWShipWakeEvent Next = Previous;
	Next.Origin = FVector2D(200.0, 300.0);
	Next.Forward = FVector2D(0.0, 1.0);
	Next.TrajectoryPoints = { Next.Origin, FVector2D(0.0, 300.0) };
	Next.UpdateServerTime = 10.1;
	Next.Amplitude = 200.0f;
	Next.SpeedCmPerSecond = 600.0f;
	Next.AdvectionSpeedCmPerSecond = 700.0f;

	const FSWShipWakeEvent Resolved = InterpolateWakeSample(Previous, Next, 10.05);
	TestTrue(TEXT("Interpolated event is active at its fixed-step time"), Resolved.IsActiveAt(10.05));
	TestTrue(TEXT("Origin X is continuous"), FMath::IsNearlyEqual(Resolved.Origin.X, 150.0, 0.01));
	TestTrue(TEXT("Origin Y is continuous"), FMath::IsNearlyEqual(Resolved.Origin.Y, 250.0, 0.01));
	TestTrue(TEXT("Amplitude is continuous"), FMath::IsNearlyEqual(Resolved.Amplitude, 150.0f, 0.01f));
	TestTrue(TEXT("Spectrum speed is continuous"),
		FMath::IsNearlyEqual(Resolved.SpeedCmPerSecond, 500.0f, 0.01f));
	TestTrue(TEXT("Advection speed is continuous"),
		FMath::IsNearlyEqual(Resolved.AdvectionSpeedCmPerSecond, 600.0f, 0.01f));
	TestTrue(TEXT("Timeline timestamp equals evaluation time"),
		FMath::IsNearlyEqual(Resolved.UpdateServerTime, 10.05, 1.e-6));
	TestTrue(TEXT("First trajectory point follows the interpolated apex"),
		Resolved.TrajectoryPoints[0].Equals(Resolved.Origin, 0.01));

	TArray<FSWShipWakeEvent> Timeline { Previous, Next };
	FSWShipWakeEvent Later = Next;
	Later.Origin = FVector2D(300.0, 400.0);
	Later.UpdateServerTime = 10.2;
	Timeline.Add(Later);
	TArray<FSWShipWakeEvent> TimelineResult;
	ResolveWakeSamplesAtTime(Timeline, 10.05, TimelineResult);
	TestEqual(TEXT("A bracketed timeline resolves exactly one vessel"), TimelineResult.Num(), 1);
	if (TimelineResult.Num() == 1)
	{
		TestTrue(TEXT("Resolved vessel never becomes future-inactive at its fixed step"),
			TimelineResult[0].IsActiveAt(10.05));
		TestTrue(TEXT("Timeline uses the samples bracketing the fixed step"),
			TimelineResult[0].Origin.Equals(FVector2D(150.0, 250.0), 0.01));
	}
	ResolveWakeSamplesAtTime(Timeline, 10.15, TimelineResult);
	TestEqual(TEXT("Later fixed step also resolves exactly one vessel"), TimelineResult.Num(), 1);
	if (TimelineResult.Num() == 1)
	{
		TestTrue(TEXT("Later state remains active instead of producing StepEvents=0"),
			TimelineResult[0].IsActiveAt(10.15));
	}
	return true;
}
#endif

