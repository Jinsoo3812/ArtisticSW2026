#include "SWShipWakeSubsystem.h"

#include "SWKelvinWakeAtlas.h"

#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWShipWake, Log, All);

namespace SWShipWakeMaterial
{
	const FName TextureParameter(TEXT("ShipWakeTex"));
	const FName TrajectoryTextureParameter(TEXT("ShipWakeTrajectoryTex"));
	const FName AtlasTextureParameter(TEXT("ShipWakeAtlas"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
	const FName HeightFieldParameter(TEXT("ShipWakeHeightField"));
	const FName FieldCenterParameter(TEXT("ShipWakeFieldCenter"));
	const FName FieldSizeParameter(TEXT("ShipWakeFieldSizeCm"));

	const FName PreviousHeightParameter(TEXT("PreviousHeightState"));
	const FName CurrentHeightParameter(TEXT("CurrentHeightState"));
	const FName PreviousCenterParameter(TEXT("PreviousStateCenter"));
	const FName CurrentCenterParameter(TEXT("CurrentStateCenter"));
	const FName OutputCenterParameter(TEXT("OutputStateCenter"));
	const FName UpdateFieldSizeParameter(TEXT("FieldSizeCm"));
	const FName FieldResolutionParameter(TEXT("FieldResolution"));
	const FName DeltaTimeParameter(TEXT("SimulationDeltaTime"));
	const FName WaveSpeedParameter(TEXT("FieldWaveSpeed"));
	const FName DampingParameter(TEXT("FieldDamping"));
	const FName SourceRateParameter(TEXT("FieldSourceRate"));
}

namespace SWShipWakeField
{
	TAutoConsoleVariable<int32> Resolution(
		TEXT("sw.ShipWake.FieldResolution"), 512,
		TEXT("M3 signed ship-wake field resolution per axis."));
	TAutoConsoleVariable<float> WorldSizeCm(
		TEXT("sw.ShipWake.FieldWorldSizeCm"), 40000.0f,
		TEXT("M3 signed ship-wake field width and height in centimeters."));
	TAutoConsoleVariable<float> SimulationHz(
		TEXT("sw.ShipWake.FieldSimulationHz"), 30.0f,
		TEXT("M3 signed ship-wake field fixed update rate."));
	TAutoConsoleVariable<float> WaveSpeedCmPerSecond(
		TEXT("sw.ShipWake.FieldWaveSpeed"), 650.0f,
		TEXT("Finite-difference propagation speed for the M3 wake field."));
	TAutoConsoleVariable<float> DampingPerSecond(
		TEXT("sw.ShipWake.FieldDamping"), 0.85f,
		TEXT("Velocity damping per second for the M3 wake field."));
	TAutoConsoleVariable<float> SourceRate(
		TEXT("sw.ShipWake.FieldSourceRate"), 2.0f,
		TEXT("Bow/stern signed source injection rate."));
}

void USWShipWakeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FSWKelvinWakeAtlas::Get().Initialize();
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
	HeightStates.Reset();
	HeightStateCenters.Reset();
	HeightFieldUpdateMID = nullptr;
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
	if (!IsRunningDedicatedServer())
	{
		UpdateTexture(ServerTime);
		BindToWaterMaterials(ServerTime);
	}
}

void USWShipWakeSubsystem::AddOrUpdateEvent(const FSWShipWakeEvent& Event)
{
	if (Event.EventId == 0 || Event.Amplitude <= 0.0f || Event.StateLifetime <= 0.0f)
	{
		return;
	}

	FWriteScopeLock Lock(EventsLock);
	if (FSWShipWakeEvent* Existing = Events.FindByPredicate(
		[&Event](const FSWShipWakeEvent& Candidate) { return Candidate.EventId == Event.EventId; }))
	{
		*Existing = Event;
		return;
	}

	while (Events.Num() >= WakeCapacity)
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
	OutEvents.Reset();
	OutEvents.Reserve(Events.Num());
	for (const FSWShipWakeEvent& Event : Events)
	{
		if (Event.IsActiveAt(ServerTime))
		{
			OutEvents.Add(Event);
		}
	}
}

float USWShipWakeSubsystem::GetWakeHeight(const FVector& WorldPosition, const double ServerTime) const
{
	FReadScopeLock Lock(EventsLock);
	return FSWShipWakeEvaluator::EvaluateHeight(FVector2D(WorldPosition), ServerTime, Events);
}

FVector2D USWShipWakeSubsystem::GetWakeGradient(const FVector& WorldPosition, const double ServerTime) const
{
	FReadScopeLock Lock(EventsLock);
	return FSWShipWakeEvaluator::EvaluateGradient(FVector2D(WorldPosition), ServerTime, Events);
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

UTextureRenderTarget2D* USWShipWakeSubsystem::CreateHeightRenderTarget(const FName& Name)
{
	const int32 FieldResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 128, 1024);
	UTextureRenderTarget2D* RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		this,
		FieldResolution,
		FieldResolution,
		ETextureRenderTargetFormat::RTF_RGBA16f,
		FLinearColor::Transparent,
		false);
	if (RenderTarget)
	{
		RenderTarget->Rename(*Name.ToString(), this);
		RenderTarget->SRGB = false;
		RenderTarget->Filter = TF_Bilinear;
		RenderTarget->AddressX = TA_Clamp;
		RenderTarget->AddressY = TA_Clamp;
		RenderTarget->UpdateResourceImmediate(true);
	}
	return RenderTarget;
}

bool USWShipWakeSubsystem::InitializeHeightField()
{
	if (bHeightFieldInitialized)
	{
		return true;
	}
	if (IsRunningDedicatedServer() || !GetWorld())
	{
		return false;
	}

	UMaterialInterface* UpdateMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/New/Water/Realistic_Water/M_SWShipWakeFieldUpdate.M_SWShipWakeFieldUpdate"));
	if (!UpdateMaterial)
	{
		UE_LOG(LogSWShipWake, Warning, TEXT("M3 update material is missing: M_SWShipWakeFieldUpdate"));
		return false;
	}

	HeightStates.SetNum(3);
	HeightStateCenters.SetNumZeroed(3);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		HeightStates[Index] = CreateHeightRenderTarget(
			FName(*FString::Printf(TEXT("SWShipWakeHeightState%d"), Index)));
		if (!HeightStates[Index])
		{
			HeightStates.Reset();
			HeightStateCenters.Reset();
			return false;
		}
		UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightStates[Index], FLinearColor::Transparent);
	}

	HeightFieldUpdateMID = UMaterialInstanceDynamic::Create(UpdateMaterial, this);
	if (!HeightFieldUpdateMID)
	{
		HeightStates.Reset();
		HeightStateCenters.Reset();
		return false;
	}

	PreviousHeightStateIndex = 0;
	CurrentHeightStateIndex = 1;
	NextHeightStateIndex = 2;
	const FVector2D InitialCenter = ResolveDesiredFieldCenter(GetServerTime());
	for (FVector2D& Center : HeightStateCenters)
	{
		Center = InitialCenter;
	}
	bHeightFieldInitialized = true;
	UE_LOG(
		LogSWShipWake,
		Display,
		TEXT("M3 signed wake field initialized: Resolution=%d SizeCm=%.0f"),
		FMath::Clamp(SWShipWakeField::Resolution.GetValueOnGameThread(), 128, 1024),
		FMath::Max(SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f));
	return true;
}

FVector2D USWShipWakeSubsystem::ResolveDesiredFieldCenter(const double ServerTime) const
{
	const float FieldSize = FMath::Max(SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f);
	const int32 FieldResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 128, 1024);
	const float TexelWorldSize = FieldSize / static_cast<float>(FieldResolution);

	FReadScopeLock Lock(EventsLock);
	const FSWShipWakeEvent* NewestEvent = nullptr;
	for (const FSWShipWakeEvent& Event : Events)
	{
		if (Event.IsActiveAt(ServerTime)
			&& (!NewestEvent || Event.UpdateServerTime > NewestEvent->UpdateServerTime))
		{
			NewestEvent = &Event;
		}
	}
	if (!NewestEvent)
	{
		return HeightStateCenters.IsValidIndex(CurrentHeightStateIndex)
			? HeightStateCenters[CurrentHeightStateIndex]
			: FVector2D::ZeroVector;
	}

	const FVector2D Forward = NewestEvent->Forward.IsNearlyZero()
		? FVector2D(1.0, 0.0)
		: NewestEvent->Forward.GetSafeNormal();
	const float Age = static_cast<float>(ServerTime - NewestEvent->UpdateServerTime);
	const FVector2D PredictedStern = NewestEvent->Origin
		+ Forward * NewestEvent->AdvectionSpeedCmPerSecond * FMath::Clamp(Age, 0.0f, 0.20f);
	const FVector2D ShipCenter = PredictedStern + Forward * NewestEvent->HullLengthCm * 0.5f;
	return FVector2D(
		FMath::GridSnap(ShipCenter.X, TexelWorldSize),
		FMath::GridSnap(ShipCenter.Y, TexelWorldSize));
}

void USWShipWakeSubsystem::StepHeightField(const double ServerTime, const float DeltaTime)
{
	if (!bHeightFieldInitialized
		|| !HeightStates.IsValidIndex(PreviousHeightStateIndex)
		|| !HeightStates.IsValidIndex(CurrentHeightStateIndex)
		|| !HeightStates.IsValidIndex(NextHeightStateIndex)
		|| !HeightFieldUpdateMID
		|| !WakeTexture)
	{
		return;
	}

	const float FieldSize = FMath::Max(SWShipWakeField::WorldSizeCm.GetValueOnGameThread(), 1000.0f);
	const int32 FieldResolution = FMath::Clamp(
		SWShipWakeField::Resolution.GetValueOnGameThread(), 128, 1024);
	const FVector2D OutputCenter = ResolveDesiredFieldCenter(ServerTime);
	if (FVector2D::Distance(OutputCenter, HeightStateCenters[CurrentHeightStateIndex]) > FieldSize * 0.45f)
	{
		for (int32 Index = 0; Index < HeightStates.Num(); ++Index)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightStates[Index], FLinearColor::Transparent);
			HeightStateCenters[Index] = OutputCenter;
		}
	}

	UTextureRenderTarget2D* PreviousState = HeightStates[PreviousHeightStateIndex];
	UTextureRenderTarget2D* CurrentState = HeightStates[CurrentHeightStateIndex];
	UTextureRenderTarget2D* NextState = HeightStates[NextHeightStateIndex];
	HeightFieldUpdateMID->SetTextureParameterValue(SWShipWakeMaterial::PreviousHeightParameter, PreviousState);
	HeightFieldUpdateMID->SetTextureParameterValue(SWShipWakeMaterial::CurrentHeightParameter, CurrentState);
	HeightFieldUpdateMID->SetTextureParameterValue(SWShipWakeMaterial::TextureParameter, WakeTexture);
	HeightFieldUpdateMID->SetScalarParameterValue(SWShipWakeMaterial::TimeParameter, static_cast<float>(ServerTime));
	HeightFieldUpdateMID->SetScalarParameterValue(SWShipWakeMaterial::CountParameter, static_cast<float>(LastUploadedCount));
	HeightFieldUpdateMID->SetVectorParameterValue(
		SWShipWakeMaterial::PreviousCenterParameter,
		FLinearColor(HeightStateCenters[PreviousHeightStateIndex].X, HeightStateCenters[PreviousHeightStateIndex].Y, 0.0f, 0.0f));
	HeightFieldUpdateMID->SetVectorParameterValue(
		SWShipWakeMaterial::CurrentCenterParameter,
		FLinearColor(HeightStateCenters[CurrentHeightStateIndex].X, HeightStateCenters[CurrentHeightStateIndex].Y, 0.0f, 0.0f));
	HeightFieldUpdateMID->SetVectorParameterValue(
		SWShipWakeMaterial::OutputCenterParameter,
		FLinearColor(OutputCenter.X, OutputCenter.Y, 0.0f, 0.0f));
	HeightFieldUpdateMID->SetScalarParameterValue(SWShipWakeMaterial::UpdateFieldSizeParameter, FieldSize);
	HeightFieldUpdateMID->SetScalarParameterValue(SWShipWakeMaterial::FieldResolutionParameter, static_cast<float>(FieldResolution));
	HeightFieldUpdateMID->SetScalarParameterValue(SWShipWakeMaterial::DeltaTimeParameter, DeltaTime);
	HeightFieldUpdateMID->SetScalarParameterValue(
		SWShipWakeMaterial::WaveSpeedParameter,
		FMath::Max(SWShipWakeField::WaveSpeedCmPerSecond.GetValueOnGameThread(), 0.0f));
	HeightFieldUpdateMID->SetScalarParameterValue(
		SWShipWakeMaterial::DampingParameter,
		FMath::Max(SWShipWakeField::DampingPerSecond.GetValueOnGameThread(), 0.0f));
	HeightFieldUpdateMID->SetScalarParameterValue(
		SWShipWakeMaterial::SourceRateParameter,
		FMath::Max(SWShipWakeField::SourceRate.GetValueOnGameThread(), 0.0f));

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NextState, HeightFieldUpdateMID);
	HeightStateCenters[NextHeightStateIndex] = OutputCenter;
	const int32 RecycledStateIndex = PreviousHeightStateIndex;
	PreviousHeightStateIndex = CurrentHeightStateIndex;
	CurrentHeightStateIndex = NextHeightStateIndex;
	NextHeightStateIndex = RecycledStateIndex;
}

UTextureRenderTarget2D* USWShipWakeSubsystem::GetHeightField() const
{
	return bHeightFieldInitialized && HeightStates.IsValidIndex(CurrentHeightStateIndex)
		? HeightStates[CurrentHeightStateIndex]
		: nullptr;
}

void USWShipWakeSubsystem::BindToWaterMaterials(const double ServerTime)
{
	if (!GetWorld() || !WakeTexture || !TrajectoryTexture || !KelvinAtlasTexture)
	{
		return;
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
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::CountParameter, static_cast<float>(LastUploadedCount));
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::TimeParameter, static_cast<float>(ServerTime));
	}
}

