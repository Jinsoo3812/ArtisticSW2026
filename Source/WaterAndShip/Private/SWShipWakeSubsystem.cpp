#include "SWShipWakeSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

namespace SWShipWakeMaterial
{
	const FName TextureParameter(TEXT("ShipWakeTex"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
}

void USWShipWakeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningDedicatedServer())
	{
		return;
	}

	WakeTexture = UTexture2D::CreateTransient(WakeCapacity, 3, PF_A32B32G32R32F, TEXT("SWShipWakeTex"));
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
}

void USWShipWakeSubsystem::Deinitialize()
{
	{
		FWriteScopeLock Lock(EventsLock);
		Events.Reset();
	}
	WakeTexture = nullptr;
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
	if (Event.EventId == 0 || Event.InitialAmplitude <= 0.0f || Event.Lifetime <= 0.0f)
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
			if (Events[Index].StartServerTime < Events[OldestIndex].StartServerTime)
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
	if (!WakeTexture)
	{
		return;
	}

	TArray<FSWShipWakeEvent> ActiveEvents;
	GetActiveEventsSnapshot(ServerTime, ActiveEvents);
	ActiveEvents.Sort([](const FSWShipWakeEvent& A, const FSWShipWakeEvent& B)
	{
		return A.StartServerTime < B.StartServerTime;
	});
	const int32 Count = FMath::Min(ActiveEvents.Num(), WakeCapacity);

	TArray<FLinearColor> Pixels;
	Pixels.SetNumZeroed(WakeCapacity * 3);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWShipWakeEvent& Event = ActiveEvents[Index];
		Pixels[Index] = FLinearColor(
			Event.Origin.X,
			Event.Origin.Y,
			static_cast<float>(Event.StartServerTime),
			Event.InitialAmplitude);
		Pixels[Index + WakeCapacity] = FLinearColor(
			Event.Forward.X,
			Event.Forward.Y,
			Event.WaveLength,
			Event.PhaseSpeed);
		Pixels[Index + WakeCapacity * 2] = FLinearColor(
			Event.Lifetime,
			Event.KelvinHalfAngleRadians,
			static_cast<float>(Event.EventId),
			1.0f);
	}

	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(WakeTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeTexture)(
			[TextureResource, Data = MoveTemp(Pixels)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, USWShipWakeSubsystem::WakeCapacity, 3);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					USWShipWakeSubsystem::WakeCapacity * sizeof(FLinearColor),
					reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
	LastUploadedCount = Count;
}

void USWShipWakeSubsystem::BindToWaterMaterials(const double ServerTime)
{
	if (!GetWorld() || !WakeTexture)
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
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::CountParameter, static_cast<float>(LastUploadedCount));
		WaterMID->SetScalarParameterValue(SWShipWakeMaterial::TimeParameter, static_cast<float>(ServerTime));
	}
}

