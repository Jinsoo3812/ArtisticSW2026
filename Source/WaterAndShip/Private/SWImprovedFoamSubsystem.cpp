#include "SWImprovedFoamSubsystem.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SWShipWakeSubsystem.h"
#include "RippleSubsystem.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWImprovedFoam, Log, All);

class FSWImprovedFoamHistoryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSWImprovedFoamHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FSWImprovedFoamHistoryCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, PreviousHistory)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreviousHistorySampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, KelvinSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, KelvinSourceSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, RippleSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, RippleSourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutHistory)
		SHADER_PARAMETER(FVector2f, PreviousCenter)
		SHADER_PARAMETER(FVector2f, CurrentCenter)
		SHADER_PARAMETER(float, FieldSize)
		SHADER_PARAMETER(FVector2f, KelvinCenter)
		SHADER_PARAMETER(float, KelvinSize)
		SHADER_PARAMETER(float, KelvinEnabled)
		SHADER_PARAMETER(FVector2f, RippleCenter)
		SHADER_PARAMETER(float, RippleSize)
		SHADER_PARAMETER(float, RippleEnabled)
		SHADER_PARAMETER(float, DeltaSeconds)
		SHADER_PARAMETER(float, KelvinLifetime)
		SHADER_PARAMETER(float, RippleLifetime)
		SHADER_PARAMETER(float, KelvinGenerationRate)
		SHADER_PARAMETER(float, RippleGenerationRate)
		SHADER_PARAMETER(float, KelvinDiffusion)
		SHADER_PARAMETER(float, RippleDiffusion)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FSWImprovedFoamHistoryCS,
	"/Project/Shaders/SWImprovedFoamHistoryCS.usf",
	"MainCS",
	SF_Compute);

namespace
{
	TAutoConsoleVariable<int32> CVarFoamEnable(
		TEXT("sw.Foam.Enable"), 1, TEXT("Enable Kelvin/Ripple Improved Foam history."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarFoamResolution(
		TEXT("sw.Foam.HistoryResolution"), 512,
		TEXT("Improved Foam history resolution (256, 512, 1024)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinLifetime(
		TEXT("sw.Foam.Kelvin.Lifetime"), 6.0f, TEXT("Kelvin Foam lifetime seconds."), ECVF_Default);
	TAutoConsoleVariable<float> CVarRippleLifetime(
		TEXT("sw.Foam.Ripple.Lifetime"), 2.5f, TEXT("Ripple Foam lifetime seconds."), ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinGenerationRate(
		TEXT("sw.Foam.Kelvin.GenerationRate"), 2.0f, TEXT("Kelvin Foam history injection rate."), ECVF_Default);
	TAutoConsoleVariable<float> CVarRippleGenerationRate(
		TEXT("sw.Foam.Ripple.GenerationRate"), 2.5f, TEXT("Ripple Foam history injection rate."), ECVF_Default);
	TAutoConsoleVariable<float> CVarKelvinDiffusion(
		TEXT("sw.Foam.Kelvin.Diffusion"), 0.0f, TEXT("Kelvin Foam diffusion per second."), ECVF_Default);
	TAutoConsoleVariable<float> CVarRippleDiffusion(
		TEXT("sw.Foam.Ripple.Diffusion"), 0.0f, TEXT("Ripple Foam diffusion per second."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarFoamDiagnostics(
		TEXT("sw.Foam.Diagnostics"), 0, TEXT("Enable Improved Foam readback diagnostics."), ECVF_Default);

	const FName ImprovedFoamStateParameter(TEXT("SW Improved Foam State"));
	const FName ImprovedFoamEnableParameter(TEXT("SW Improved Foam Enable"));
}

void USWImprovedFoamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDiagnosticsEnabled = FParse::Param(FCommandLine::Get(), TEXT("SWFoamDiagnostics"));
#if !UE_BUILD_SHIPPING
	bInjectKelvinTest = FParse::Param(FCommandLine::Get(), TEXT("SWFoamInjectKelvin"));
#endif
	if (!IsRunningDedicatedServer())
	{
		CreateHistoryResources();
	}
}

void USWImprovedFoamSubsystem::Deinitialize()
{
	WaterMaterials.Reset();
	HistoryA = nullptr;
	HistoryB = nullptr;
	Super::Deinitialize();
}

void USWImprovedFoamSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RefreshWaterMaterials();
	UE_LOG(LogSWImprovedFoam, Display,
		TEXT("[SW-FOAM][INIT] World=%s Resolution=%d FieldSize=%.1f Diagnostics=%s"),
		*InWorld.GetName(), Resolution, FieldSizeCm, bDiagnosticsEnabled ? TEXT("true") : TEXT("false"));
}

TStatId USWImprovedFoamSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWImprovedFoamSubsystem, STATGROUP_Tickables);
}

UTextureRenderTarget2D* USWImprovedFoamSubsystem::CreateHistoryTarget(const FName& Name) const
{
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(
		const_cast<USWImprovedFoamSubsystem*>(this), Name);
	if (!Target)
	{
		return nullptr;
	}
	Target->RenderTargetFormat = RTF_RGBA16f;
	Target->ClearColor = FLinearColor::Black;
	Target->bAutoGenerateMips = false;
	Target->bCanCreateUAV = true;
	Target->Filter = TF_Bilinear;
	Target->AddressX = TA_Clamp;
	Target->AddressY = TA_Clamp;
	Target->InitAutoFormat(Resolution, Resolution);
	Target->UpdateResourceImmediate(true);
	return Target;
}

void USWImprovedFoamSubsystem::CreateHistoryResources()
{
	Resolution = FMath::Clamp(CVarFoamResolution.GetValueOnGameThread(), 256, 1024);
	HistoryA = CreateHistoryTarget(TEXT("SWImprovedFoam_HistoryA"));
	HistoryB = CreateHistoryTarget(TEXT("SWImprovedFoam_HistoryB"));
	bCurrentIsA = true;
	bHasHistory = false;
	DispatchCount = 0;
	UE_LOG(LogSWImprovedFoam, Display,
		TEXT("[SW-FOAM][INIT] HistoryResources A=%s B=%s Format=RGBA16F Resolution=%d"),
		*GetNameSafe(HistoryA), *GetNameSafe(HistoryB), Resolution);
}

FVector2D USWImprovedFoamSubsystem::ResolveFallbackCenter() const
{
	FVector CameraLocation = FVector::ZeroVector;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FRotator Rotation;
			PC->GetPlayerViewPoint(CameraLocation, Rotation);
		}
	}
	const float TexelSize = FieldSizeCm / FMath::Max(Resolution, 1);
	return FVector2D(FMath::GridSnap(CameraLocation.X, TexelSize), FMath::GridSnap(CameraLocation.Y, TexelSize));
}

void USWImprovedFoamSubsystem::DispatchHistory(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !HistoryA || !HistoryB)
	{
		return;
	}

	USWShipWakeSubsystem* WakeSubsystem = World->GetSubsystem<USWShipWakeSubsystem>();
	URippleSubsystem* RippleSubsystem = World->GetSubsystem<URippleSubsystem>();
	UTextureRenderTarget2D* KelvinSource = WakeSubsystem ? WakeSubsystem->GetWakeFoamSourceRenderTarget() : nullptr;
	UTextureRenderTarget2D* RippleSource = RippleSubsystem ? RippleSubsystem->GetRippleFoamSourceRenderTarget() : nullptr;
	if (!KelvinSource || !RippleSource)
	{
		if (bDiagnosticsEnabled)
		{
			UE_LOG(LogSWImprovedFoam, Warning,
				TEXT("[SW-FOAM][ERROR] Source unavailable Kelvin=%s Ripple=%s"),
				*GetNameSafe(KelvinSource), *GetNameSafe(RippleSource));
		}
		return;
	}

	PreviousCenter = bHasHistory ? CurrentCenter : (WakeSubsystem ? WakeSubsystem->GetWakeGridCenter() : ResolveFallbackCenter());
	FieldSizeCm = WakeSubsystem ? WakeSubsystem->GetWakeGridSize() : 30000.0f;
	CurrentCenter = WakeSubsystem ? WakeSubsystem->GetWakeGridCenter() : ResolveFallbackCenter();

	UTextureRenderTarget2D* Previous = bCurrentIsA ? HistoryA : HistoryB;
	UTextureRenderTarget2D* Output = bCurrentIsA ? HistoryB : HistoryA;
	FTextureResource* PreviousResource = Previous->GetResource();
	FTextureResource* OutputResource = Output->GetResource();
	FTextureResource* KelvinResource = KelvinSource->GetResource();
	FTextureResource* RippleResource = RippleSource->GetResource();
	if (!PreviousResource || !OutputResource || !KelvinResource || !RippleResource)
	{
		return;
	}

	const FVector2f PreviousCenterF(PreviousCenter.X, PreviousCenter.Y);
	const FVector2f CurrentCenterF(CurrentCenter.X, CurrentCenter.Y);
	const FVector2D KelvinCenterD = WakeSubsystem->GetWakeGridCenter();
	const FVector2D RippleCenterD = RippleSubsystem->GetRippleGridCenter();
	const FVector2f KelvinCenterF(KelvinCenterD.X, KelvinCenterD.Y);
	const FVector2f RippleCenterF(RippleCenterD.X, RippleCenterD.Y);
	const float KelvinSize = WakeSubsystem->GetWakeGridSize();
	const float RippleSize = RippleSubsystem->GetRippleGridSize();
	const float SafeDelta = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	const int32 DispatchResolution = Resolution;

	ENQUEUE_RENDER_COMMAND(DispatchSWImprovedFoamHistory)(
		[PreviousResource, OutputResource, KelvinResource, RippleResource,
		 PreviousCenterF, CurrentCenterF, KelvinCenterF, RippleCenterF,
		 FieldSize = FieldSizeCm, KelvinSize, RippleSize, SafeDelta, DispatchResolution](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* PreviousRHI = PreviousResource->GetTexture2DRHI();
			FRHITexture* OutputRHI = OutputResource->GetTexture2DRHI();
			FRHITexture* KelvinRHI = KelvinResource->GetTexture2DRHI();
			FRHITexture* RippleRHI = RippleResource->GetTexture2DRHI();
			if (!PreviousRHI || !OutputRHI || !KelvinRHI || !RippleRHI)
			{
				return;
			}

			TShaderMapRef<FSWImprovedFoamHistoryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			if (!ComputeShader.IsValid())
			{
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef OutputRDG = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(OutputRHI, TEXT("SWImprovedFoamHistoryOutput")));
			FSWImprovedFoamHistoryCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FSWImprovedFoamHistoryCS::FParameters>();
			Parameters->PreviousHistory = PreviousRHI;
			Parameters->PreviousHistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->KelvinSource = KelvinRHI;
			Parameters->KelvinSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->RippleSource = RippleRHI;
			Parameters->RippleSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->OutHistory = GraphBuilder.CreateUAV(OutputRDG);
			Parameters->PreviousCenter = PreviousCenterF;
			Parameters->CurrentCenter = CurrentCenterF;
			Parameters->FieldSize = FieldSize;
			Parameters->KelvinCenter = KelvinCenterF;
			Parameters->KelvinSize = KelvinSize;
			Parameters->KelvinEnabled = 1.0f;
			Parameters->RippleCenter = RippleCenterF;
			Parameters->RippleSize = RippleSize;
			Parameters->RippleEnabled = 1.0f;
			Parameters->DeltaSeconds = SafeDelta;
			Parameters->KelvinLifetime = FMath::Max(CVarKelvinLifetime.GetValueOnRenderThread(), 0.01f);
			Parameters->RippleLifetime = FMath::Max(CVarRippleLifetime.GetValueOnRenderThread(), 0.01f);
			Parameters->KelvinGenerationRate = FMath::Max(CVarKelvinGenerationRate.GetValueOnRenderThread(), 0.0f);
			Parameters->RippleGenerationRate = FMath::Max(CVarRippleGenerationRate.GetValueOnRenderThread(), 0.0f);
			Parameters->KelvinDiffusion = FMath::Max(CVarKelvinDiffusion.GetValueOnRenderThread(), 0.0f);
			Parameters->RippleDiffusion = FMath::Max(CVarRippleDiffusion.GetValueOnRenderThread(), 0.0f);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SWImprovedFoamHistoryCS"),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(
					FIntVector(DispatchResolution, DispatchResolution, 1), FIntVector(16, 16, 1)));
			GraphBuilder.Execute();
		});

	bCurrentIsA = !bCurrentIsA;
	bHasHistory = true;
	++DispatchCount;
}

void USWImprovedFoamSubsystem::RefreshWaterMaterials()
{
	WaterMaterials.Reset();
	if (!GetWorld())
	{
		return;
	}
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		if (UMaterialInstanceDynamic* MID = Component ? Component->GetWaterMaterialInstance() : nullptr)
		{
			WaterMaterials.AddUnique(MID);
		}
	}
}

void USWImprovedFoamSubsystem::BindToWaterMaterials()
{
	UTextureRenderTarget2D* State = GetCurrentHistory();
	LastBoundMaterialCount = 0;
	for (int32 Index = WaterMaterials.Num() - 1; Index >= 0; --Index)
	{
		UMaterialInstanceDynamic* MID = WaterMaterials[Index].Get();
		if (!MID)
		{
			WaterMaterials.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		MID->SetTextureParameterValue(ImprovedFoamStateParameter, State);
		MID->SetScalarParameterValue(
			ImprovedFoamEnableParameter,
			CVarFoamEnable.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);
		++LastBoundMaterialCount;
	}
}

UTextureRenderTarget2D* USWImprovedFoamSubsystem::GetCurrentHistory() const
{
	return bCurrentIsA ? HistoryA : HistoryB;
}

void USWImprovedFoamSubsystem::LogRenderTargetStats(
	const TCHAR* Label, UTextureRenderTarget2D* Target, const bool bHistory) const
{
	if (!Target)
	{
		return;
	}
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		UE_LOG(LogSWImprovedFoam, Warning, TEXT("[SW-FOAM][ERROR] Readback failed Label=%s"), Label);
		return;
	}

	TArray<FLinearColor> Pixels;
	if (bHistory)
	{
		TArray<FFloat16Color> Float16Pixels;
		if (!Resource->ReadFloat16Pixels(Float16Pixels) || Float16Pixels.IsEmpty())
		{
			UE_LOG(LogSWImprovedFoam, Warning, TEXT("[SW-FOAM][ERROR] Readback failed Label=%s"), Label);
			return;
		}
		Pixels.Reserve(Float16Pixels.Num());
		for (const FFloat16Color& Pixel : Float16Pixels)
		{
			Pixels.Emplace(Pixel.R.GetFloat(), Pixel.G.GetFloat(), Pixel.B.GetFloat(), Pixel.A.GetFloat());
		}
	}
	else if (!Resource->ReadLinearColorPixels(Pixels) || Pixels.IsEmpty())
	{
		UE_LOG(LogSWImprovedFoam, Warning, TEXT("[SW-FOAM][ERROR] Readback failed Label=%s"), Label);
		return;
	}

	double SumR = 0.0;
	double SumG = 0.0;
	float MaxR = 0.0f;
	float MaxG = 0.0f;
	int32 CoveredR = 0;
	int32 CoveredG = 0;
	const int32 Stride = FMath::Max(Pixels.Num() / 16384, 1);
	int32 Samples = 0;
	for (int32 Index = 0; Index < Pixels.Num(); Index += Stride)
	{
		const float R = Pixels[Index].R;
		const float G = Pixels[Index].G;
		SumR += R;
		SumG += G;
		MaxR = FMath::Max(MaxR, R);
		MaxG = FMath::Max(MaxG, G);
		CoveredR += R > 0.01f ? 1 : 0;
		CoveredG += G > 0.01f ? 1 : 0;
		++Samples;
	}
	UE_LOG(LogSWImprovedFoam, Display,
		TEXT("[SW-FOAM][%s] Samples=%d RMean=%.6f RMax=%.6f RCoverage=%.3f%% GMean=%.6f GMax=%.6f GCoverage=%.3f%% History=%s"),
		Label, Samples,
		Samples ? SumR / Samples : 0.0, MaxR, Samples ? 100.0 * CoveredR / Samples : 0.0,
		Samples ? SumG / Samples : 0.0, MaxG, Samples ? 100.0 * CoveredG / Samples : 0.0,
		bHistory ? TEXT("true") : TEXT("false"));
}

void USWImprovedFoamSubsystem::TickDiagnostics(const float DeltaTime)
{
	bDiagnosticsEnabled = bDiagnosticsEnabled || CVarFoamDiagnostics.GetValueOnGameThread() != 0;
	if (!bDiagnosticsEnabled)
	{
		return;
	}
	SummaryAccumulator += DeltaTime;
	ReadbackAccumulator += DeltaTime;
	if (SummaryAccumulator >= 1.0f)
	{
		SummaryAccumulator = 0.0f;
		UE_LOG(LogSWImprovedFoam, Display,
			TEXT("[SW-FOAM][GRID] Dispatch=%llu Center=(%.1f,%.1f) Previous=(%.1f,%.1f) Size=%.1f Materials=%d"),
			DispatchCount, CurrentCenter.X, CurrentCenter.Y, PreviousCenter.X, PreviousCenter.Y,
			FieldSizeCm, LastBoundMaterialCount);
	}
	if (ReadbackAccumulator >= 5.0f)
	{
		ReadbackAccumulator = 0.0f;
		LogRenderTargetStats(TEXT("HISTORY"), GetCurrentHistory(), true);
		if (UWorld* World = GetWorld())
		{
			if (USWShipWakeSubsystem* Wake = World->GetSubsystem<USWShipWakeSubsystem>())
			{
				LogRenderTargetStats(TEXT("KELVIN-SOURCE"), Wake->GetWakeFoamSourceRenderTarget(), false);
			}
			if (URippleSubsystem* Ripple = World->GetSubsystem<URippleSubsystem>())
			{
				LogRenderTargetStats(TEXT("RIPPLE-SOURCE"), Ripple->GetRippleFoamSourceRenderTarget(), false);
			}
		}
	}
}

void USWImprovedFoamSubsystem::Tick(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || IsRunningDedicatedServer() || !World->IsGameWorld())
	{
		return;
	}

	const int32 DesiredResolution = FMath::Clamp(CVarFoamResolution.GetValueOnGameThread(), 256, 1024);
	if (!HistoryA || !HistoryB || DesiredResolution != Resolution)
	{
		CreateHistoryResources();
	}

#if !UE_BUILD_SHIPPING
	// Explicit command-line test hook only. It validates the full Golden A ->
	// Kelvin source UAV -> history path without changing normal gameplay state.
	if (bInjectKelvinTest && !bKelvinTestInjected)
	{
		if (USWShipWakeSubsystem* Wake = World->GetSubsystem<USWShipWakeSubsystem>())
		{
			const FVector2D Center = ResolveFallbackCenter();
			const double Now = Wake->GetServerTime();
			FSWShipWakeEvent TestEvent;
			TestEvent.Origin = Center - FVector2D(1000.0, 0.0);
			TestEvent.EndOrigin = Center;
			TestEvent.Forward = FVector2D(1.0, 0.0);
			TestEvent.EndForward = TestEvent.Forward;
			TestEvent.StartServerTime = Now - 3.0;
			TestEvent.EndServerTime = Now - 2.0;
			TestEvent.ExpireServerTime = Now + 10.0;
			TestEvent.InitialAmplitudeCm = 120.0f;
			TestEvent.PropagationSpeedCmPerSecond = 1200.0f;
			TestEvent.DecayRate = 0.02f;
			TestEvent.WakeLengthCm = 25000.0f;
			TestEvent.WakeHalfWidthCm = 12000.0f;
			TestEvent.FadeInSeconds = 0.08f;
			TestEvent.FroudeProfile = ESWKelvinFroudeProfile::Fr_0_50;
			bKelvinTestInjected = Wake->SubmitPredictedEvent(TestEvent);
			UE_LOG(LogSWImprovedFoam, Display,
				TEXT("[SW-FOAM][TEST] KelvinInjected=%s Center=(%.1f,%.1f) Start=%.3f"),
				bKelvinTestInjected ? TEXT("true") : TEXT("false"), Center.X, Center.Y,
				TestEvent.StartServerTime);
		}
	}
#endif

	MaterialRefreshAccumulator += DeltaTime;
	if (MaterialRefreshAccumulator >= 1.0f || WaterMaterials.IsEmpty())
	{
		MaterialRefreshAccumulator = 0.0f;
		RefreshWaterMaterials();
	}

	if (CVarFoamEnable.GetValueOnGameThread() != 0)
	{
		DispatchHistory(DeltaTime);
	}
	BindToWaterMaterials();
	TickDiagnostics(DeltaTime);
}
