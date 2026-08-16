#include "SWShipWakeSubsystem.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "SWKelvinWakeAtlas.h"
#include "SWShipWakeReplicator.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWShipWake, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarEnable(
		TEXT("sw.ShipWake.Enable"), 1, TEXT("Enable M7 Golden Image Kelvin wake."));
	TAutoConsoleVariable<int32> CVarDebugLog(
		TEXT("sw.ShipWake.DebugLog"), 0, TEXT("Log compact M7 event state once per second."));
	constexpr float PredictionDistanceCm = 750.0f;
	constexpr double PredictionTimeSeconds = 1.0;

	const FName EventTextureParameter(TEXT("ShipWakeTex"));
	const FName GoldenTextureParameter(TEXT("ShipWakeGolden"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
	const FName EnableParameter(TEXT("ShipWakeEnable"));
}

void USWShipWakeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FSWKelvinWakeAtlas::Get().Initialize();
	if (IsRunningDedicatedServer()) return;

	EventTexture = UTexture2D::CreateTransient(WakeCapacity, 4, PF_A32B32G32R32F,
		TEXT("SWShipWakeM7Events"));
	if (EventTexture)
	{
		EventTexture->SRGB = false;
		EventTexture->CompressionSettings = TC_VectorDisplacementmap;
		EventTexture->Filter = TF_Nearest;
		EventTexture->AddressX = TA_Clamp;
		EventTexture->AddressY = TA_Clamp;
		EventTexture->NeverStream = true;
		EventTexture->UpdateResource();
	}
	GoldenTexture = FSWKelvinWakeAtlas::Get().CreateTransientTexture(TEXT("SWKelvinWakeM7Golden"));
}

void USWShipWakeSubsystem::Deinitialize()
{
	{
		FWriteScopeLock Lock(EventsLock);
		Events.Reset();
	}
	WaterMaterials.Reset();
	Replicator.Reset();
	EventTexture = nullptr;
	GoldenTexture = nullptr;
	Super::Deinitialize();
}

void USWShipWakeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() != NM_Client && !Replicator.IsValid())
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("SWShipWakeReplicator");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		InWorld.SpawnActor<ASWShipWakeReplicator>(
			ASWShipWakeReplicator::StaticClass(), FTransform::Identity, Params);
	}
	RefreshWaterMaterials();
}

void USWShipWakeSubsystem::Tick(const float DeltaTime)
{
	const double ServerTime = GetServerTime();
	RemoveExpiredEvents(ServerTime);
	if (!IsRunningDedicatedServer())
	{
		UpdateEventTexture();
		MaterialRefreshAccumulator += DeltaTime;
		if (MaterialRefreshAccumulator >= 1.0f || WaterMaterials.IsEmpty())
		{
			MaterialRefreshAccumulator = 0.0f;
			RefreshWaterMaterials();
		}
		BindToWaterMaterials(ServerTime);
	}
	if (CVarDebugLog.GetValueOnGameThread() != 0)
	{
		static double LastLog = -DBL_MAX;
		if (ServerTime - LastLog >= 1.0)
		{
			LastLog = ServerTime;
			UE_LOG(LogSWShipWake, Warning,
				TEXT("[M7Runtime] NetMode=%d Enable=%d Stored=%d Uploaded=%d Revision=%u"),
				static_cast<int32>(GetWorld()->GetNetMode()), CVarEnable.GetValueOnGameThread(),
				GetEventCount(), LastUploadedCount, Revision.Load());
		}
	}
}

TStatId USWShipWakeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWShipWakeSubsystem, STATGROUP_Tickables);
}

bool USWShipWakeSubsystem::SubmitAuthoritativeEvent(const FSWShipWakeEvent& EventTemplate)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
	ASWShipWakeReplicator* WakeReplicator = Replicator.Get();
	return WakeReplicator && WakeReplicator->AddServerEvent(EventTemplate);
}

bool USWShipWakeSubsystem::SubmitPredictedEvent(const FSWShipWakeEvent& EventTemplate)
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Client) return false;
	FSWShipWakeEvent Event = EventTemplate;
	Event.EventId = NextPredictedEventId--;
	AddOrUpdateCapped(Event);
	return true;
}

void USWShipWakeSubsystem::AddOrUpdateReplicatedEvent(const FSWShipWakeEvent& Event)
{
	FSWShipWakeEvent EventToStore = Event;
	{
		FWriteScopeLock Lock(EventsLock);
		const int32 ExistingIndex = Events.IndexOfByPredicate([&Event](const FSWShipWakeEvent& Existing)
		{
			return Existing.EventId == Event.EventId;
		});
		if (ExistingIndex != INDEX_NONE)
		{
			const double Lifetime = Event.ExpireServerTime - Event.StartServerTime;
			EventToStore.StartServerTime = Events[ExistingIndex].StartServerTime;
			EventToStore.ExpireServerTime = EventToStore.StartServerTime + Lifetime;
			Events[ExistingIndex] = EventToStore;
			++Revision;
			return;
		}

		if (GetWorld() && GetWorld()->GetNetMode() == NM_Client && Event.EventId > 0)
		{
			int32 BestIndex = INDEX_NONE;
			float BestDistanceSquared = FMath::Square(PredictionDistanceCm);
			for (int32 Index = 0; Index < Events.Num(); ++Index)
			{
				const FSWShipWakeEvent& Candidate = Events[Index];
				const float DistanceSquared = FVector2D::DistSquared(Candidate.Origin, Event.Origin);
				if (Candidate.EventId < 0 && DistanceSquared <= BestDistanceSquared
					&& FMath::Abs(Candidate.StartServerTime - Event.StartServerTime) <= PredictionTimeSeconds
					&& FVector2D::DotProduct(Candidate.Forward, Event.Forward) >= 0.7f)
				{
					BestIndex = Index;
					BestDistanceSquared = DistanceSquared;
				}
			}
			if (BestIndex != INDEX_NONE)
			{
				const double Lifetime = Event.ExpireServerTime - Event.StartServerTime;
				EventToStore.StartServerTime = Events[BestIndex].StartServerTime;
				EventToStore.ExpireServerTime = EventToStore.StartServerTime + Lifetime;
				Events.RemoveAtSwap(BestIndex, 1, EAllowShrinking::No);
			}
		}
		while (Events.Num() >= WakeCapacity)
		{
			int32 Oldest = 0;
			for (int32 Index = 1; Index < Events.Num(); ++Index)
			{
				if (Events[Index].StartServerTime < Events[Oldest].StartServerTime) Oldest = Index;
			}
			Events.RemoveAtSwap(Oldest, 1, EAllowShrinking::No);
		}
		Events.Add(EventToStore);
	}
	++Revision;
}

void USWShipWakeSubsystem::AddOrUpdateCapped(const FSWShipWakeEvent& Event)
{
	{
		FWriteScopeLock Lock(EventsLock);
		while (Events.Num() >= WakeCapacity)
		{
			int32 Oldest = 0;
			for (int32 Index = 1; Index < Events.Num(); ++Index)
			{
				if (Events[Index].StartServerTime < Events[Oldest].StartServerTime) Oldest = Index;
			}
			Events.RemoveAtSwap(Oldest, 1, EAllowShrinking::No);
		}
		Events.Add(Event);
	}
	++Revision;
}

void USWShipWakeSubsystem::RegisterReplicator(ASWShipWakeReplicator* InReplicator)
{
	Replicator = InReplicator;
}

void USWShipWakeSubsystem::GetEventsSnapshot(TArray<FSWShipWakeEvent>& OutEvents) const
{
	FReadScopeLock Lock(EventsLock);
	OutEvents = Events;
}

void USWShipWakeSubsystem::GetActiveEventsSnapshot(
	const double ServerTime, TArray<FSWShipWakeEvent>& OutEvents) const
{
	OutEvents.Reset();
	FReadScopeLock Lock(EventsLock);
	for (const FSWShipWakeEvent& Event : Events)
	{
		if (Event.IsActiveAt(ServerTime)) OutEvents.Add(Event);
	}
}

float USWShipWakeSubsystem::GetWakeHeight(
	const FVector& WorldPosition, const double ServerTime) const
{
	if (CVarEnable.GetValueOnAnyThread() == 0) return 0.0f;
	TArray<FSWShipWakeEvent, TInlineAllocator<WakeCapacity>> Active;
	{
		FReadScopeLock Lock(EventsLock);
		for (const FSWShipWakeEvent& Event : Events)
		{
			if (Event.IsActiveAt(ServerTime)) Active.Add(Event);
		}
	}
	return FSWShipWakeEvaluator::EvaluateHeight(FVector2D(WorldPosition), ServerTime, Active);
}

FVector2D USWShipWakeSubsystem::GetWakeGradient(
	const FVector& WorldPosition, const double ServerTime) const
{
	if (CVarEnable.GetValueOnAnyThread() == 0) return FVector2D::ZeroVector;
	TArray<FSWShipWakeEvent, TInlineAllocator<WakeCapacity>> Active;
	{
		FReadScopeLock Lock(EventsLock);
		for (const FSWShipWakeEvent& Event : Events)
		{
			if (Event.IsActiveAt(ServerTime)) Active.Add(Event);
		}
	}
	return FSWShipWakeEvaluator::EvaluateGradient(FVector2D(WorldPosition), ServerTime, Active);
}

double USWShipWakeSubsystem::GetServerTime() const
{
	if (!GetWorld()) return 0.0;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return GetWorld()->GetTimeSeconds();
}

int32 USWShipWakeSubsystem::GetEventCount() const
{
	FReadScopeLock Lock(EventsLock);
	return Events.Num();
}

void USWShipWakeSubsystem::RemoveExpiredEvents(const double ServerTime)
{
	bool bRemoved = false;
	{
		FWriteScopeLock Lock(EventsLock);
		bRemoved = Events.RemoveAllSwap([ServerTime, Retention = PhysicsHistoryRetentionSeconds](
			const FSWShipWakeEvent& Event)
		{
			return Event.ExpireServerTime + Retention < ServerTime;
		}, EAllowShrinking::No) > 0;
	}
	if (bRemoved) ++Revision;
}

void USWShipWakeSubsystem::UpdateEventTexture()
{
	if (!EventTexture || UploadedRevision == Revision.Load()) return;
	TArray<FSWShipWakeEvent> Snapshot;
	GetEventsSnapshot(Snapshot);
	Snapshot.Sort([](const FSWShipWakeEvent& A, const FSWShipWakeEvent& B)
	{
		return A.StartServerTime < B.StartServerTime;
	});
	const int32 Count = FMath::Min(Snapshot.Num(), WakeCapacity);
	TArray<FLinearColor> Pixels;
	Pixels.SetNumZeroed(WakeCapacity * 4);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWShipWakeEvent& E = Snapshot[Index];
		Pixels[Index] = FLinearColor(E.Origin.X, E.Origin.Y, E.Forward.X, E.Forward.Y);
		Pixels[Index + WakeCapacity] = FLinearColor(
			static_cast<float>(E.StartServerTime), E.InitialAmplitudeCm,
			E.PropagationSpeedCmPerSecond, E.DecayRate);
		Pixels[Index + WakeCapacity * 2] = FLinearColor(
			static_cast<float>(E.ExpireServerTime), E.WakeLengthCm,
			E.WakeHalfWidthCm, E.EnvelopeWidthCm);
		Pixels[Index + WakeCapacity * 3] = FLinearColor(E.FadeInSeconds, 0.0f, 0.0f, 0.0f);
	}
	if (FTexture2DResource* Resource = static_cast<FTexture2DResource*>(EventTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeM7Events)(
			[Resource, Data = MoveTemp(Pixels)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, WakeCapacity, 4);
				RHICmdList.UpdateTexture2D(Resource->GetTexture2DRHI(), 0, Region,
					WakeCapacity * sizeof(FLinearColor), reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
	LastUploadedCount = Count;
	UploadedRevision = Revision.Load();
}

void USWShipWakeSubsystem::RefreshWaterMaterials()
{
	WaterMaterials.Reset();
	if (!GetWorld()) return;
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		if (UMaterialInstanceDynamic* MID = Component ? Component->GetWaterMaterialInstance() : nullptr)
		{
			WaterMaterials.AddUnique(MID);
		}
	}
}

void USWShipWakeSubsystem::BindToWaterMaterials(const double ServerTime)
{
	if (!EventTexture || !GoldenTexture) return;
	for (int32 Index = WaterMaterials.Num() - 1; Index >= 0; --Index)
	{
		UMaterialInstanceDynamic* MID = WaterMaterials[Index].Get();
		if (!MID)
		{
			WaterMaterials.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		MID->SetTextureParameterValue(EventTextureParameter, EventTexture);
		MID->SetTextureParameterValue(GoldenTextureParameter, GoldenTexture);
		MID->SetScalarParameterValue(CountParameter, static_cast<float>(LastUploadedCount));
		MID->SetScalarParameterValue(TimeParameter, static_cast<float>(ServerTime));
		MID->SetScalarParameterValue(EnableParameter,
			CVarEnable.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);
	}
}
