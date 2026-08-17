#include "SWShipWakeSubsystem.h"

#include "Engine/Engine.h"
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
	TAutoConsoleVariable<int32> CVarFroudeProfile(
		TEXT("sw.ShipWake.FroudeProfile"), -1,
		TEXT("Override Froude Profile: -1=Auto/Emitter, 0=Fr0.30, 1=Fr0.50, 2=Fr0.70, 3=Fr1.00"));
	TAutoConsoleVariable<int32> CVarMaxCapacity(
		TEXT("sw.ShipWake.MaxCapacity"), USWShipWakeSubsystem::DefaultWakeCapacity,
		TEXT("Maximum active Kelvin wake buffer capacity (1-1024). Dynamically cached on change."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarOnScreenDebug(
		TEXT("sw.ShipWake.OnScreenDebug"), 1,
		TEXT("Display real-time Kelvin wake buffer metrics (Current/Max) on top of the screen (0=Off, 1=On)."),
		ECVF_Default);

	constexpr float PredictionDistanceCm = 750.0f;
	constexpr double PredictionTimeSeconds = 1.0;

	const FName EventTextureParameter(TEXT("ShipWakeTex"));
	const FName GoldenTextureParameter(TEXT("ShipWakeGolden"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
	const FName EnableParameter(TEXT("ShipWakeEnable"));

	TAtomic<int32> GCachedMaxCapacity(USWShipWakeSubsystem::DefaultWakeCapacity);
}

int32 USWShipWakeSubsystem::GetMaxCapacity()
{
	const int32 CVarVal = CVarMaxCapacity.GetValueOnAnyThread();
	const int32 Clamped = FMath::Clamp(CVarVal, 1, MaxWakeCapacity);
	GCachedMaxCapacity.Store(Clamped);
	return Clamped;
}

void USWShipWakeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FSWKelvinWakeAtlas::Get().Initialize();
	if (IsRunningDedicatedServer()) return;

	EventTexture = UTexture2D::CreateTransient(MaxWakeCapacity, 4, PF_A32B32G32R32F,
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

	const ESWKelvinFroudeProfile Profiles[FSWKelvinWakeAtlas::ProfileCount] = {
		ESWKelvinFroudeProfile::Fr_0_30,
		ESWKelvinFroudeProfile::Fr_0_50,
		ESWKelvinFroudeProfile::Fr_0_70,
		ESWKelvinFroudeProfile::Fr_1_00
	};
	GoldenTextures.SetNumZeroed(FSWKelvinWakeAtlas::ProfileCount);
	for (int32 Index = 0; Index < FSWKelvinWakeAtlas::ProfileCount; ++Index)
	{
		const FName TexName(*FString::Printf(TEXT("SWKelvinWakeM7Golden_P%d"), Index));
		GoldenTextures[Index] = FSWKelvinWakeAtlas::Get().CreateTransientTexture(Profiles[Index], TexName);
	}
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
	GoldenTextures.Reset();
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

	const UWorld* World = GetWorld();
	const bool bIsRenderingClient = World && World->IsGameWorld()
		&& (World->GetNetMode() != NM_DedicatedServer)
		&& (World->GetFirstLocalPlayerFromController() != nullptr || World->GetNetMode() == NM_Standalone);

	if (bIsRenderingClient)
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

	// 온스크린 디버그 메시지: 화면 상단에 실시간 버퍼 수치 표시 (현재/최대 버퍼 수)
	if (CVarOnScreenDebug.GetValueOnGameThread() > 0 && GEngine && bIsRenderingClient)
	{
		const int32 StoredCount = GetEventCount();
		const int32 MaxCap = GetMaxCapacity();
		int32 ActiveCount = 0;
		{
			FReadScopeLock Lock(EventsLock);
			for (const FSWShipWakeEvent& Event : Events)
			{
				if (Event.IsActiveAt(ServerTime))
				{
					++ActiveCount;
				}
			}
		}

		const uint64 DebugKey = 0x53574B454C56494E; // "SWKELVIN"
		const FString DebugMsg = FString::Printf(
			TEXT("[Kelvin Wake Buffer] Current: %d / Max: %d (Active: %d, CVar: %d) | ServerTime: %.2fs"),
			StoredCount, MaxCap, ActiveCount, CVarMaxCapacity.GetValueOnGameThread(), ServerTime);

		const FColor MsgColor = (StoredCount >= MaxCap) ? FColor::Yellow : FColor::Cyan;
		GEngine->AddOnScreenDebugMessage(DebugKey, 0.0f, MsgColor, DebugMsg, true, FVector2D(1.15f, 1.15f));
	}

	const int32 DebugLogLevel = CVarDebugLog.GetValueOnGameThread();
	if (DebugLogLevel > 0 && bIsRenderingClient)
	{
		// First event origin initialization as fixed probe point
		{
			FReadScopeLock Lock(EventsLock);
			if (!bHasLockedProbe && !Events.IsEmpty())
			{
				const FSWShipWakeEvent& FirstE = Events[0];
				LockedProbeLocation = FirstE.Origin - FirstE.Forward * 2000.0f; // 20m behind first emission
				bHasLockedProbe = true;
			}
		}

		if (DebugLogLevel == 1)
		{
			static double LastLog = -DBL_MAX;
			if (ServerTime - LastLog >= 1.0)
			{
				LastLog = ServerTime;
				float CpuSampleHeight = 0.0f;
				FString FirstEventInfo = TEXT("None");
				{
					FReadScopeLock Lock(EventsLock);
					if (!Events.IsEmpty())
					{
						const FSWShipWakeEvent& E0 = Events.Last();
						const double Age = ServerTime - E0.StartServerTime;
						const FVector2D TestPos = E0.Origin - E0.Forward * 1000.0f;
						CpuSampleHeight = GetWakeHeight(FVector(TestPos.X, TestPos.Y, 0.0), ServerTime);
						FirstEventInfo = FString::Printf(
							TEXT("Origin=(%.0f, %.0f), Fwd=(%.2f, %.2f), Amp=%.1f, Age=%.2fs, CpuHeightBehind10m=%.2fcm"),
							E0.Origin.X, E0.Origin.Y, E0.Forward.X, E0.Forward.Y, E0.InitialAmplitudeCm, Age, CpuSampleHeight);
					}
				}
				UTexture2D* ActiveGolden = GetActiveGoldenTexture();
				const TCHAR* ProfileNames[] = { TEXT("Fr0.30"), TEXT("Fr0.50"), TEXT("Fr0.70"), TEXT("Fr1.00") };
				const int32 ProfileIdx = FMath::Clamp(static_cast<int32>(GetActiveFroudeProfile()), 0, 3);
				UE_LOG(LogSWShipWake, Warning,
					TEXT("[M7Runtime] NetMode=%d Enable=%d Profile=%s Stored=%d/%d Uploaded=%d Rev=%u Mats=%d EventTex=%d GoldenTex=%d Time=%.2f | LastEvent: %s"),
					static_cast<int32>(GetWorld()->GetNetMode()), CVarEnable.GetValueOnGameThread(),
					ProfileNames[ProfileIdx],
					GetEventCount(), GetMaxCapacity(), LastUploadedCount, Revision.Load(),
					WaterMaterials.Num(), EventTexture != nullptr, ActiveGolden != nullptr, ServerTime,
					*FirstEventInfo);
			}
		}
		else if (DebugLogLevel >= 2 && bHasLockedProbe)
		{
			// Level 2 & 3: Frame-by-frame continuous time-series probe at fixed coordinate
			FSWShipWakeDebugSample Sample;
			{
				FReadScopeLock Lock(EventsLock);
				Sample = FSWShipWakeEvaluator::EvaluateDebug(
					LockedProbeLocation, ServerTime, Events, DebugLogLevel >= 3);
			}

			if (DebugLogLevel == 2)
			{
				UE_LOG(LogSWShipWake, Warning,
					TEXT("[WakeProbe] Frame=%llu Time=%.3fs | Height=%6.2fcm (Weight=%.3f, Active=%d/%d) @ Probe=(%.0f, %.0f)"),
					GFrameCounter, ServerTime, Sample.FinalHeight, Sample.BlendWeight,
					Sample.ActiveContributingEvents, Sample.TotalEventsChecked,
					LockedProbeLocation.X, LockedProbeLocation.Y);
			}
			else // Level 3: Full detail
			{
				UE_LOG(LogSWShipWake, Warning,
					TEXT("[WakeProbe-Detail] Frame=%llu Time=%.3fs | Height=%6.2fcm (Weight=%.3f, Active=%d) | %s"),
					GFrameCounter, ServerTime, Sample.FinalHeight, Sample.BlendWeight,
					Sample.ActiveContributingEvents,
					Sample.DetailLog.IsEmpty() ? TEXT("[No Active Events In Envelope]") : *Sample.DetailLog);
			}
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
	if (!GetWorld() || IsRunningDedicatedServer()) return false;
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
		const int32 MaxCapacity = GetMaxCapacity();
		while (Events.Num() >= MaxCapacity)
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
		const int32 MaxCapacity = GetMaxCapacity();
		while (Events.Num() >= MaxCapacity)
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
	TArray<FSWShipWakeEvent> Active;
	{
		FReadScopeLock Lock(EventsLock);
		Active.Reserve(FMath::Min(Events.Num(), 128));
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
	TArray<FSWShipWakeEvent> Active;
	{
		FReadScopeLock Lock(EventsLock);
		Active.Reserve(FMath::Min(Events.Num(), 128));
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
	const int32 MaxCap = GetMaxCapacity();
	const int32 Count = FMath::Min(Snapshot.Num(), MaxCap);
	TArray<FLinearColor> Pixels;
	Pixels.SetNumZeroed(MaxWakeCapacity * 4);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWShipWakeEvent& E = Snapshot[Index];
		Pixels[Index] = FLinearColor(E.Origin.X, E.Origin.Y, E.Forward.X, E.Forward.Y);
		Pixels[Index + MaxWakeCapacity] = FLinearColor(
			static_cast<float>(E.StartServerTime), E.InitialAmplitudeCm,
			E.PropagationSpeedCmPerSecond, E.DecayRate);
		Pixels[Index + MaxWakeCapacity * 2] = FLinearColor(
			static_cast<float>(E.ExpireServerTime), E.WakeLengthCm,
			E.WakeHalfWidthCm, E.EnvelopeWidthCm);
		Pixels[Index + MaxWakeCapacity * 3] = FLinearColor(
			E.FadeInSeconds, E.LengthCutRatio, E.WidthCutRatio, 0.0f);
	}
	if (FTexture2DResource* Resource = static_cast<FTexture2DResource*>(EventTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeM7Events)(
			[Resource, Data = MoveTemp(Pixels)](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, MaxWakeCapacity, 4);
				RHICmdList.UpdateTexture2D(Resource->GetTexture2DRHI(), 0, Region,
					MaxWakeCapacity * sizeof(FLinearColor), reinterpret_cast<const uint8*>(Data.GetData()));
			});
	}
	LastUploadedCount = Count;
	UploadedRevision = Revision.Load();
}

void USWShipWakeSubsystem::RefreshWaterMaterials()
{
	WaterMaterials.Reset();
	if (!GetWorld()) return;
	int32 WaterBodyCount = 0;
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		++WaterBodyCount;
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		if (UMaterialInstanceDynamic* MID = Component ? Component->GetWaterMaterialInstance() : nullptr)
		{
			WaterMaterials.AddUnique(MID);
			if (CVarDebugLog.GetValueOnGameThread() != 0)
			{
				FString ParentName = MID->Parent ? MID->Parent->GetName() : TEXT("None");
				UE_LOG(LogSWShipWake, Log, TEXT("[M7Runtime] Bound WaterMaterial: %s (Parent: %s, Actor: %s)"),
					*MID->GetName(), *ParentName, *It->GetName());
			}
		}
	}
	if (CVarDebugLog.GetValueOnGameThread() != 0 && WaterMaterials.IsEmpty())
	{
		UE_LOG(LogSWShipWake, Warning,
			TEXT("[M7Runtime] RefreshWaterMaterials found %d AWaterBody actors but 0 dynamic water materials!"),
			WaterBodyCount);
	}
}

void USWShipWakeSubsystem::SetActiveFroudeProfile(const ESWKelvinFroudeProfile Profile)
{
	ActiveFroudeProfile = Profile;
}

ESWKelvinFroudeProfile USWShipWakeSubsystem::GetActiveFroudeProfile() const
{
	const int32 OverrideIdx = CVarFroudeProfile.GetValueOnGameThread();
	if (OverrideIdx >= 0 && OverrideIdx < FSWKelvinWakeAtlas::ProfileCount)
	{
		return static_cast<ESWKelvinFroudeProfile>(OverrideIdx);
	}
	{
		FReadScopeLock Lock(EventsLock);
		if (!Events.IsEmpty())
		{
			return Events.Last().FroudeProfile;
		}
	}
	return ActiveFroudeProfile;
}

UTexture2D* USWShipWakeSubsystem::GetActiveGoldenTexture() const
{
	const ESWKelvinFroudeProfile Profile = GetActiveFroudeProfile();
	int32 ProfileIndex = 1;
	switch (Profile)
	{
	case ESWKelvinFroudeProfile::Fr_0_30: ProfileIndex = 0; break;
	case ESWKelvinFroudeProfile::Fr_0_50: ProfileIndex = 1; break;
	case ESWKelvinFroudeProfile::Fr_0_70: ProfileIndex = 2; break;
	case ESWKelvinFroudeProfile::Fr_1_00: ProfileIndex = 3; break;
	default: ProfileIndex = 1; break;
	}
	if (GoldenTextures.IsValidIndex(ProfileIndex))
	{
		return GoldenTextures[ProfileIndex];
	}
	return GoldenTextures.IsEmpty() ? nullptr : GoldenTextures[0];
}

void USWShipWakeSubsystem::BindToWaterMaterials(const double ServerTime)
{
	UTexture2D* ActiveGolden = GetActiveGoldenTexture();
	if (!EventTexture || !ActiveGolden) return;
	for (int32 Index = WaterMaterials.Num() - 1; Index >= 0; --Index)
	{
		UMaterialInstanceDynamic* MID = WaterMaterials[Index].Get();
		if (!MID)
		{
			WaterMaterials.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		MID->SetTextureParameterValue(EventTextureParameter, EventTexture);
		MID->SetTextureParameterValue(GoldenTextureParameter, ActiveGolden);
		MID->SetScalarParameterValue(CountParameter, static_cast<float>(LastUploadedCount));
		MID->SetScalarParameterValue(TimeParameter, static_cast<float>(ServerTime));
		MID->SetScalarParameterValue(EnableParameter,
			CVarEnable.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);
	}
}
