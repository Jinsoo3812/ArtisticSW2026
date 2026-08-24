#include "SWShipWakeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SWKelvinWakeAtlas.h"
#include "SWShipWakeReplicator.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWShipWake, Log, All);

// ----------------------------------------------------------------------------------
// Global Compute Shader Declaration for Kelvin Wake Bake Pass
// ----------------------------------------------------------------------------------
class FSWShipWakeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSWShipWakeCS);
	SHADER_USE_PARAMETER_STRUCT(FSWShipWakeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, EventTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, EventTextureSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, GoldenTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, GoldenTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutWakeTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutKelvinFoamTexture)
		SHADER_PARAMETER(FVector2f, GridCenter)
		SHADER_PARAMETER(float, GridSize)
		SHADER_PARAMETER(float, ServerTime)
		SHADER_PARAMETER(float, EventCount)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, FoamSteepnessMin)
		SHADER_PARAMETER(float, FoamSteepnessMax)
		SHADER_PARAMETER(float, FoamIntensity)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSWShipWakeCS, "/Project/Shaders/SWShipWakeCS.usf", "MainCS", SF_Compute);

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
		TEXT("Maximum active Kelvin wake buffer capacity (1-256). Dynamically cached on change."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarOnScreenDebug(
		TEXT("sw.ShipWake.OnScreenDebug"), 1,
		TEXT("Display real-time Kelvin wake buffer metrics (Current/Max) on top of the screen (0=Off, 1=On)."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarResolution(
		TEXT("sw.ShipWake.Resolution"), 512,
		TEXT("Compute Shader Baked Render Target Resolution (256, 512, 1024)."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarGridSize(
		TEXT("sw.ShipWake.GridSize"), 30000.0f,
		TEXT("Compute Shader Baked World Coverage Area in Cm (default 30000 = 300m)."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinFoamSteepnessMin(
		TEXT("sw.Foam.Kelvin.SteepnessMin"), 0.0015f,
		TEXT("Minimum amplitude/length for Kelvin Foam emission."), ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinFoamSteepnessMax(
		TEXT("sw.Foam.Kelvin.SteepnessMax"), 0.0060f,
		TEXT("Full-strength amplitude/length for Kelvin Foam emission."), ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinFoamIntensity(
		TEXT("sw.Foam.Kelvin.Intensity"), 1.0f,
		TEXT("Kelvin Foam emission intensity."), ECVF_Default);

	constexpr float PredictionDistanceCm = 750.0f;
	constexpr double PredictionTimeSeconds = 1.0;

	const FName EventTextureParameter(TEXT("ShipWakeTex"));
	const FName GoldenTextureParameter(TEXT("ShipWakeGolden"));
	const FName CountParameter(TEXT("ShipWakeCount"));
	const FName TimeParameter(TEXT("ShipWakeServerTime"));
	const FName EnableParameter(TEXT("ShipWakeEnable"));

	// Baked Compute Shader Render Target Parameters
	const FName WakeRTParameter(TEXT("ShipWakeRT"));
	const FName GridCenterParameter(TEXT("ShipWakeGridCenter"));
	const FName GridSizeParameter(TEXT("ShipWakeGridSize"));

	TAtomic<int32> GCachedMaxCapacity(USWShipWakeSubsystem::DefaultWakeCapacity);
}

int32 USWShipWakeSubsystem::GetMaxCapacity()
{
	const int32 CVarVal = CVarMaxCapacity.GetValueOnAnyThread();
	const int32 Clamped = FMath::Clamp(CVarVal, 1, MaxWakeCapacity);
	GCachedMaxCapacity.Store(Clamped);
	return Clamped;
}

void USWShipWakeSubsystem::CreateWakeRenderTarget()
{
	if (IsRunningDedicatedServer()) return;

	RenderTargetResolution = FMath::Clamp(CVarResolution.GetValueOnGameThread(), 256, 1024);
	WakeRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SWShipWake_RenderTarget"));
	if (WakeRenderTarget)
	{
		WakeRenderTarget->RenderTargetFormat = RTF_RGBA16f;
		WakeRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		WakeRenderTarget->bAutoGenerateMips = false;
		WakeRenderTarget->bCanCreateUAV = true;
		WakeRenderTarget->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
		WakeRenderTarget->UpdateResourceImmediate(true);
	}

	WakeFoamSourceRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SWShipWake_FoamSource"));
	if (WakeFoamSourceRenderTarget)
	{
		WakeFoamSourceRenderTarget->RenderTargetFormat = RTF_R16f;
		WakeFoamSourceRenderTarget->ClearColor = FLinearColor::Black;
		WakeFoamSourceRenderTarget->bAutoGenerateMips = false;
		WakeFoamSourceRenderTarget->bCanCreateUAV = true;
		WakeFoamSourceRenderTarget->Filter = TF_Bilinear;
		WakeFoamSourceRenderTarget->AddressX = TA_Clamp;
		WakeFoamSourceRenderTarget->AddressY = TA_Clamp;
		WakeFoamSourceRenderTarget->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
		WakeFoamSourceRenderTarget->UpdateResourceImmediate(true);
	}
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

	CreateWakeRenderTarget();
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
	WakeRenderTarget = nullptr;
	WakeFoamSourceRenderTarget = nullptr;
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

FVector2D USWShipWakeSubsystem::ResolveWakeGridCenter() const
{
	FVector CameraLocation = FVector::ZeroVector;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			CameraLocation = ViewLocation;
		}
	}

	const float TexelWorldSize = GridSizeCm / FMath::Max(RenderTargetResolution, 1);
	return FVector2D(
		FMath::GridSnap(CameraLocation.X, TexelWorldSize),
		FMath::GridSnap(CameraLocation.Y, TexelWorldSize)
	);
}

void USWShipWakeSubsystem::DispatchWakeComputeShader(const double ServerTime)
{
	if (!WakeRenderTarget || !WakeFoamSourceRenderTarget || !EventTexture) return;
	UTexture2D* ActiveGolden = GetActiveGoldenTexture();
	if (!ActiveGolden) return;

	FTextureResource* RTResource = WakeRenderTarget->GetResource();
	FTextureResource* FoamRTResource = WakeFoamSourceRenderTarget->GetResource();
	if (!RTResource || !FoamRTResource) return;

	FTextureResource* EventRes = EventTexture->GetResource();
	FTextureResource* GoldenRes = ActiveGolden->GetResource();
	if (!EventRes || !GoldenRes) return;

	const int32 Count = LastUploadedCount;
	const FVector2f GridCenterFloat = FVector2f(CurrentGridCenter.X, CurrentGridCenter.Y);
	const float GridSizeFloat = GridSizeCm;
	const float ServerTimeFloat = static_cast<float>(ServerTime);
	const int32 Res = RenderTargetResolution;

	ENQUEUE_RENDER_COMMAND(DispatchSWShipWakeCS)(
		[RTResource, FoamRTResource, EventRes, GoldenRes, GridCenterFloat, GridSizeFloat, ServerTimeFloat, Count, Res](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* OutputTextureRHI = RTResource->GetTexture2DRHI();
			FRHITexture* FoamOutputTextureRHI = FoamRTResource->GetTexture2DRHI();
			if (!OutputTextureRHI || !FoamOutputTextureRHI) return;

			FRHITexture* EventTextureRHI = EventRes->GetTexture2DRHI();
			FRHITexture* GoldenTextureRHI = GoldenRes->GetTexture2DRHI();
			if (!EventTextureRHI || !GoldenTextureRHI) return;

			TShaderMapRef<FSWShipWakeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			if (!ComputeShader.IsValid()) return;

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef OutputRDG = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputTextureRHI, TEXT("SWShipWakeOutputRT")));
			FRDGTextureRef FoamOutputRDG = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(FoamOutputTextureRHI, TEXT("SWShipWakeFoamSourceRT")));
			FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(OutputRDG);

			FSWShipWakeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSWShipWakeCS::FParameters>();
			PassParameters->EventTexture = EventTextureRHI;
			PassParameters->EventTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->GoldenTexture = GoldenTextureRHI;
			PassParameters->GoldenTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->OutWakeTexture = OutputUAV;
			PassParameters->OutKelvinFoamTexture = GraphBuilder.CreateUAV(FoamOutputRDG);
			PassParameters->GridCenter = GridCenterFloat;
			PassParameters->GridSize = GridSizeFloat;
			PassParameters->ServerTime = ServerTimeFloat;
			PassParameters->EventCount = static_cast<float>(Count);
			PassParameters->NormalStrength = 1.0f;
			PassParameters->FoamSteepnessMin = CVarKelvinFoamSteepnessMin.GetValueOnRenderThread();
			PassParameters->FoamSteepnessMax = FMath::Max(
				CVarKelvinFoamSteepnessMax.GetValueOnRenderThread(),
				PassParameters->FoamSteepnessMin + 1.0e-6f);
			PassParameters->FoamIntensity = FMath::Max(CVarKelvinFoamIntensity.GetValueOnRenderThread(), 0.0f);

			FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Res, Res, 1), FIntVector(16, 16, 1));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SWShipWakeBakeCS"),
				ComputeShader,
				PassParameters,
				GroupCount
			);

			GraphBuilder.Execute();
		});
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
		GridSizeCm = CVarGridSize.GetValueOnGameThread();
		CurrentGridCenter = ResolveWakeGridCenter();

		UpdateEventTexture();
		DispatchWakeComputeShader(ServerTime);

		MaterialRefreshAccumulator += DeltaTime;
		if (MaterialRefreshAccumulator >= 1.0f || WaterMaterials.IsEmpty())
		{
			MaterialRefreshAccumulator = 0.0f;
			RefreshWaterMaterials();
		}
		BindToWaterMaterials(ServerTime);
	}

	// 온스크린 디버그 메시지
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
		const FString DebugMsg = FString::Printf(
			TEXT("Kelvin Wake [CS Baked RT]: Active=%d / Stored=%d / MaxCap=%d (Grid: %.0fm)"),
			ActiveCount, StoredCount, MaxCap, GridSizeCm * 0.01f);
		GEngine->AddOnScreenDebugMessage(
			184719, 0.0f, FColor::Cyan, DebugMsg, true, FVector2D(1.1f, 1.1f));
	}
}

TStatId USWShipWakeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWShipWakeSubsystem, STATGROUP_Tickables);
}

bool USWShipWakeSubsystem::SubmitAuthoritativeEvent(const FSWShipWakeEvent& EventTemplate)
{
	FSWShipWakeEvent Event = EventTemplate;
	if (Replicator.IsValid() && Replicator->HasAuthority())
	{
		return Replicator->AddServerEvent(Event);
	}
	AddOrUpdateCapped(Event);
	return true;
}

bool USWShipWakeSubsystem::SubmitPredictedEvent(const FSWShipWakeEvent& EventTemplate)
{
	FSWShipWakeEvent Event = EventTemplate;
	Event.EventId = NextPredictedEventId--;
	if (NextPredictedEventId > -1)
	{
		NextPredictedEventId = -1;
	}
	AddOrUpdateCapped(Event);
	return true;
}

void USWShipWakeSubsystem::AddOrUpdateReplicatedEvent(const FSWShipWakeEvent& Event)
{
	AddOrUpdateCapped(Event);
}

void USWShipWakeSubsystem::RegisterReplicator(ASWShipWakeReplicator* InReplicator)
{
	Replicator = InReplicator;
}

void USWShipWakeSubsystem::AddOrUpdateCapped(const FSWShipWakeEvent& Event)
{
	{
		FWriteScopeLock Lock(EventsLock);
		const int32 FoundIndex = Events.IndexOfByPredicate([Id = Event.EventId](const FSWShipWakeEvent& E)
		{
			return E.EventId == Id;
		});
		if (FoundIndex != INDEX_NONE)
		{
			Events[FoundIndex] = Event;
		}
		else
		{
			const int32 MaxCap = GetMaxCapacity();
			if (Events.Num() >= MaxCap)
			{
				Events.RemoveAt(0, 1, EAllowShrinking::No);
			}
			Events.Add(Event);
		}
	}
	++Revision;
}

void USWShipWakeSubsystem::GetEventsSnapshot(TArray<FSWShipWakeEvent>& OutEvents) const
{
	OutEvents.Reset();
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
			if (Event.IsActiveAt(ServerTime)) Active.Add(Event);
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
			if (Event.IsActiveAt(ServerTime)) Active.Add(Event);
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
		const FVector2D P0 = E.Origin;
		const FVector2D P1 = (E.EndOrigin.IsNearlyZero() && E.EndServerTime <= E.StartServerTime)
			? E.Origin : E.EndOrigin;
		const double T0 = E.StartServerTime;
		const double T1 = FMath::Max(E.EndServerTime, T0);
		const FVector2D Fwd0 = E.Forward.IsNearlyZero() ? FVector2D(1.0, 0.0) : E.Forward.GetSafeNormal();
		const FVector2D Fwd1 = E.EndForward.IsNearlyZero() ? Fwd0 : E.EndForward.GetSafeNormal();
		const float Yaw0 = FMath::Atan2(static_cast<float>(Fwd0.Y), static_cast<float>(Fwd0.X));
		const float Yaw1 = FMath::Atan2(static_cast<float>(Fwd1.Y), static_cast<float>(Fwd1.X));

		Pixels[Index] = FLinearColor(P0.X, P0.Y, P1.X, P1.Y);
		Pixels[Index + MaxWakeCapacity] = FLinearColor(
			static_cast<float>(T0), static_cast<float>(T1),
			E.InitialAmplitudeCm, E.PropagationSpeedCmPerSecond);
		Pixels[Index + MaxWakeCapacity * 2] = FLinearColor(
			static_cast<float>(E.ExpireServerTime), E.WakeLengthCm,
			E.WakeHalfWidthCm, E.DecayRate);
		Pixels[Index + MaxWakeCapacity * 3] = FLinearColor(
			Yaw0, Yaw1, E.FadeInSeconds, E.LengthCutRatio);
	}
	const int32 UploadCount = FMath::Clamp(Count, 1, MaxWakeCapacity);
	if (FTexture2DResource* Resource = static_cast<FTexture2DResource*>(EventTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateSWShipWakeM7Events)(
			[Resource, Data = MoveTemp(Pixels), UploadCount](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, UploadCount, 4);
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

		// 1. 하위 호환용 파라미터 유지
		MID->SetTextureParameterValue(EventTextureParameter, EventTexture);
		MID->SetTextureParameterValue(GoldenTextureParameter, ActiveGolden);
		MID->SetScalarParameterValue(CountParameter, static_cast<float>(LastUploadedCount));
		MID->SetScalarParameterValue(TimeParameter, static_cast<float>(ServerTime));
		MID->SetScalarParameterValue(EnableParameter,
			CVarEnable.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);

		// 2. 신규 Compute Shader 베이킹 렌더타깃 파라미터 전달!
		if (WakeRenderTarget)
		{
			MID->SetTextureParameterValue(WakeRTParameter, WakeRenderTarget);
			MID->SetVectorParameterValue(GridCenterParameter, FLinearColor(CurrentGridCenter.X, CurrentGridCenter.Y, 0.0f, 0.0f));
			MID->SetScalarParameterValue(GridSizeParameter, GridSizeCm);
		}
	}
}
