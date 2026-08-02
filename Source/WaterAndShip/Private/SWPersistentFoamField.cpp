#include "SWPersistentFoamField.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GerstnerWaterWaves.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "Components/SceneComponent.h"
#include "UnrealClient.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterSubsystem.h"
#include "WaterWaves.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWPersistentFoam, Log, All);

namespace SWPersistentFoam
{
	const FName PreviousStateParameter(TEXT("V5 Previous Foam State"));
	const FName PreviousCenterParameter(TEXT("V5 Previous Field Center"));
	const FName CurrentCenterParameter(TEXT("V5 Field Center"));
	const FName FieldSizeParameter(TEXT("V5 Field Size Cm"));
	const FName ResolutionParameter(TEXT("V5 Field Resolution"));
	const FName DeltaSecondsParameter(TEXT("V5 Delta Seconds"));
	const FName LifetimeParameter(TEXT("V5 Foam Lifetime Seconds"));
	const FName SourceRateParameter(TEXT("V5 Foam Source Rate"));
	const FName CrestStartParameter(TEXT("V5 Foam Crest Start Cm"));
	const FName CrestEndParameter(TEXT("V5 Foam Crest End Cm"));
	const FName SlopeStartParameter(TEXT("V5 Foam Slope Start"));
	const FName SlopeEndParameter(TEXT("V5 Foam Slope End"));
	const FName AdvectionScaleParameter(TEXT("V5 Foam Advection Scale"));
	const FName CpuWaveFieldParameter(TEXT("V5 CPU Wave Field"));
	const FName CpuWaveFieldCenterParameter(TEXT("V5 CPU Wave Field Center"));
	const FName MaxEncodedVelocityParameter(TEXT("V5 Max Encoded Velocity CmPerSecond"));
	const FName OutputStateParameter(TEXT("V5 Foam State"));

	void BlendWaveBetweenLWCTiles(
		const FGerstnerWave& Wave,
		const FVector& LocalWorldPosition,
		const float TimeSeconds,
		float& WaveSin,
		float& WaveCos)
	{
		const FVector TileBorderDistance = FVector(FLargeWorldRenderScalar::GetTileSize() * 0.5) - LocalWorldPosition.GetAbs();
		constexpr double BlendZoneWidth = 400.0;
		if (TileBorderDistance.X >= BlendZoneWidth && TileBorderDistance.Y >= BlendZoneWidth)
		{
			return;
		}

		const FVector2D BlendWorldPosition(TileBorderDistance.X, TileBorderDistance.Y);
		const double BlendAlpha =
			FMath::Clamp(BlendWorldPosition.X / BlendZoneWidth, 0.0, 1.0) *
			FMath::Clamp(BlendWorldPosition.Y / BlendZoneWidth, 0.0, 1.0);
		const float BlendPhase = FVector2D::DotProduct(BlendWorldPosition, Wave.WaveVector) - Wave.WaveSpeed * TimeSeconds;
		float BlendSin = 0.0f;
		float BlendCos = 0.0f;
		FMath::SinCos(&BlendSin, &BlendCos, BlendPhase);
		WaveSin = FMath::Lerp(BlendSin, WaveSin, BlendAlpha);
		WaveCos = FMath::Lerp(BlendCos, WaveCos, BlendAlpha);
	}

	FVector2D EvaluateExactHorizontalVelocity(
		const TArray<FGerstnerWave>& Waves,
		const FVector& WorldPosition,
		const float TimeSeconds)
	{
		const FVector LocalWorldPosition(FLargeWorldRenderPosition(WorldPosition).GetOffset());
		FVector2D Velocity = FVector2D::ZeroVector;
		for (const FGerstnerWave& Wave : Waves)
		{
			const float Phase = FVector2D::DotProduct(FVector2D(LocalWorldPosition), Wave.WaveVector) - Wave.WaveSpeed * TimeSeconds;
			float WaveSin = 0.0f;
			float WaveCos = 0.0f;
			FMath::SinCos(&WaveSin, &WaveCos, Phase);
			BlendWaveBetweenLWCTiles(Wave, LocalWorldPosition, TimeSeconds, WaveSin, WaveCos);

			// Epic's horizontal displacement is -Q * sin(phase) * Direction.
			// Its exact time derivative is Q * WaveSpeed * cos(phase) * Direction.
			Velocity += FVector2D(Wave.Direction) * (Wave.Q * Wave.WaveSpeed * WaveCos);
		}
		return Velocity;
	}
}

ASWPersistentFoamField::ASWPersistentFoamField()
{
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
}

void ASWPersistentFoamField::BeginPlay()
{
	Super::BeginPlay();
	InitializeFoamField();
}

void ASWPersistentFoamField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	WaterMID = nullptr;
	FoamUpdateMID = nullptr;
	FoamStateA = nullptr;
	FoamStateB = nullptr;
	CpuWaveField = nullptr;
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}

void ASWPersistentFoamField::ResolveTargetWaterBody()
{
	if (IsValid(TargetWaterBody) || !GetWorld())
	{
		return;
	}

	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		AWaterBody* Candidate = *It;
		if (!IsValid(Candidate) || !IsValid(Candidate->GetWaterBodyComponent()))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			TargetWaterBody = Candidate;
		}
	}
}

UTextureRenderTarget2D* ASWPersistentFoamField::CreateStateRenderTarget(const FName Name)
{
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this, Name);
	if (!RenderTarget)
	{
		return nullptr;
	}

	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->Filter = TextureFilter::TF_Bilinear;
	RenderTarget->AddressX = TextureAddress::TA_Clamp;
	RenderTarget->AddressY = TextureAddress::TA_Clamp;
	RenderTarget->InitAutoFormat(Resolution, Resolution);
	RenderTarget->UpdateResourceImmediate(true);
	return RenderTarget;
}

bool ASWPersistentFoamField::InitializeFoamField()
{
	if (bInitialized)
	{
		return true;
	}

	ResolveTargetWaterBody();
	if (!IsValid(TargetWaterBody) || !IsValid(FoamStateUpdateMaterial) ||
		Resolution < 128 || FieldWorldSizeCm <= 0.0f)
	{
		return false;
	}

	FoamStateA = CreateStateRenderTarget(TEXT("V5FoamStateA"));
	FoamStateB = CreateStateRenderTarget(TEXT("V5FoamStateB"));
	FoamUpdateMID = UMaterialInstanceDynamic::Create(FoamStateUpdateMaterial, this);
	WaterMID = TargetWaterBody->GetWaterBodyComponent()->GetWaterMaterialInstance();
	if (!FoamStateA || !FoamStateB || !FoamUpdateMID || !WaterMID || !CreateCpuWaveField())
	{
		return false;
	}

	CurrentCenter = ResolveDesiredCenter();
	PreviousCenter = CurrentCenter;
	CpuWaveFieldCenter = CurrentCenter;
	if (!UpdateCpuWaveField())
	{
		return false;
	}
	bLatestStateIsA = true;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, FoamStateA, FLinearColor::Transparent);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, FoamStateB, FLinearColor::Transparent);
	PushStateToWaterMaterial(FoamStateA);
	bInitialized = true;
	UE_LOG(
		LogSWPersistentFoam,
		Display,
		TEXT("Initialized persistent foam field: Water=%s Resolution=%d FieldSizeCm=%.1f Lifetime=%.2f"),
		*GetNameSafe(TargetWaterBody),
		Resolution,
		FieldWorldSizeCm,
		FoamLifetimeSeconds);
	return true;
}

bool ASWPersistentFoamField::CreateCpuWaveField()
{
	SourceResolution = FMath::Clamp(SourceResolution, 32, 512);
	CpuWaveField = UTexture2D::CreateTransient(SourceResolution, SourceResolution, PF_FloatRGBA, TEXT("V5CpuWaveField"));
	if (!CpuWaveField)
	{
		return false;
	}

	CpuWaveField->SRGB = false;
	CpuWaveField->Filter = TF_Bilinear;
	CpuWaveField->AddressX = TA_Clamp;
	CpuWaveField->AddressY = TA_Clamp;
	CpuWaveField->NeverStream = true;
	CpuWaveField->UpdateResource();
	return true;
}

bool ASWPersistentFoamField::UpdateCpuWaveField()
{
	UWaterBodyComponent* WaterComponent = TargetWaterBody ? TargetWaterBody->GetWaterBodyComponent() : nullptr;
	const UWaterWavesBase* WavesBase = WaterComponent ? WaterComponent->GetWaterWaves() : nullptr;
	const UGerstnerWaterWaves* GerstnerWaves = WavesBase ? Cast<UGerstnerWaterWaves>(WavesBase->GetWaterWaves()) : nullptr;
	if (!WaterComponent || !CpuWaveField || !CpuWaveField->GetPlatformData() ||
		CpuWaveField->GetPlatformData()->Mips.IsEmpty() || !GerstnerWaves)
	{
		UE_LOG(LogSWPersistentFoam, Warning, TEXT("CPU wave field unavailable: Water=%s Waves=%s Texture=%s"),
			*GetNameSafe(TargetWaterBody), *GetNameSafe(GerstnerWaves), *GetNameSafe(CpuWaveField));
		return false;
	}

	const UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
	const float WaterTimeSeconds = WaterSubsystem ? WaterSubsystem->GetWaterTimeSeconds() : GetWorld()->GetTimeSeconds();
	const TArray<FGerstnerWave>& Waves = GerstnerWaves->GetGerstnerWaves();
	const float SafeVelocityRange = FMath::Max(MaxEncodedVelocityCmPerSecond, 10.0f);
	const float PixelWorldSize = FieldWorldSizeCm / static_cast<float>(SourceResolution);
	CpuWaveFieldCenter = CurrentCenter;

	const int32 PixelCount = SourceResolution * SourceResolution;
	uint8* UploadData = new uint8[PixelCount * sizeof(FFloat16Color)];
	FFloat16Color* Pixels = reinterpret_cast<FFloat16Color*>(UploadData);
	for (int32 Y = 0; Y < SourceResolution; ++Y)
	{
		for (int32 X = 0; X < SourceResolution; ++X)
		{
			const FVector WorldPosition(
				CpuWaveFieldCenter.X + (static_cast<float>(X) + 0.5f - SourceResolution * 0.5f) * PixelWorldSize,
				CpuWaveFieldCenter.Y + (static_cast<float>(Y) + 0.5f - SourceResolution * 0.5f) * PixelWorldSize,
				TargetWaterBody->GetActorLocation().Z);

			FVector SurfaceNormal = FVector::UpVector;
			const float Height = WavesBase->GetWaveHeightAtPosition(WorldPosition, 100000.0f, WaterTimeSeconds, SurfaceNormal);
			const float Slope = FVector2D(SurfaceNormal.X, SurfaceNormal.Y).Length() / FMath::Max(FMath::Abs(SurfaceNormal.Z), 1.0e-4f);
			const float Crest = FMath::SmoothStep(CrestStartCm, FMath::Max(CrestEndCm, CrestStartCm + 1.0f), Height);
			const float Breaking = FMath::SmoothStep(SlopeStart, FMath::Max(SlopeEnd, SlopeStart + 1.0e-3f), Slope);
			const float Source = FMath::Clamp(Crest * Breaking, 0.0f, 1.0f);

			const FVector2D Velocity = SWPersistentFoam::EvaluateExactHorizontalVelocity(Waves, WorldPosition, WaterTimeSeconds);
			const float EncodedVelocityX = FMath::Clamp(Velocity.X / SafeVelocityRange * 0.5f + 0.5f, 0.0f, 1.0f);
			const float EncodedVelocityY = FMath::Clamp(Velocity.Y / SafeVelocityRange * 0.5f + 0.5f, 0.0f, 1.0f);
			Pixels[Y * SourceResolution + X] = FFloat16Color(FLinearColor(Source, EncodedVelocityX, EncodedVelocityY, 1.0f));
		}
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
		0,
		0,
		0,
		0,
		SourceResolution,
		SourceResolution);
	CpuWaveField->UpdateTextureRegions(
		0,
		1,
		Region,
		SourceResolution * sizeof(FFloat16Color),
		sizeof(FFloat16Color),
		UploadData,
		[](uint8* SourceData, const FUpdateTextureRegion2D* SourceRegion)
		{
			delete[] SourceData;
			delete SourceRegion;
		});
	return true;
}

FVector2D ASWPersistentFoamField::ResolveDesiredCenter() const
{
	FVector Location = GetActorLocation();
	if (bFollowLocalView && GetWorld())
	{
		if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			Location = ViewLocation;
		}
	}

	const float TexelWorldSize = FieldWorldSizeCm / FMath::Max(Resolution, 1);
	return FVector2D(
		FMath::GridSnap(Location.X, TexelWorldSize),
		FMath::GridSnap(Location.Y, TexelWorldSize));
}

void ASWPersistentFoamField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bInitialized && !InitializeFoamField())
	{
		return;
	}

	UWaterBodyComponent* WaterComponent = TargetWaterBody ? TargetWaterBody->GetWaterBodyComponent() : nullptr;
	if (!IsValid(WaterComponent) || !IsValid(FoamUpdateMID))
	{
		return;
	}

	PreviousCenter = CurrentCenter;
	CurrentCenter = ResolveDesiredCenter();
	if (FVector2D::Distance(CurrentCenter, PreviousCenter) > FieldWorldSizeCm * 0.45f)
	{
		ResetFoamState();
		PreviousCenter = CurrentCenter;
	}

	UTextureRenderTarget2D* PreviousState = bLatestStateIsA ? FoamStateA : FoamStateB;
	UTextureRenderTarget2D* WriteState = bLatestStateIsA ? FoamStateB : FoamStateA;
	if (!PreviousState || !WriteState)
	{
		return;
	}

	const float SafeDeltaSeconds = FMath::Clamp(DeltaSeconds, 1.0f / 240.0f, 1.0f / 15.0f);
	SourceUpdateElapsedSeconds += DeltaSeconds;
	if (SourceUpdateElapsedSeconds >= FMath::Max(SourceUpdateIntervalSeconds, 0.016f))
	{
		if (UpdateCpuWaveField())
		{
			SourceUpdateElapsedSeconds = 0.0f;
		}
	}
	FoamUpdateMID->SetTextureParameterValue(SWPersistentFoam::PreviousStateParameter, PreviousState);
	FoamUpdateMID->SetTextureParameterValue(SWPersistentFoam::CpuWaveFieldParameter, CpuWaveField);
	FoamUpdateMID->SetVectorParameterValue(
		SWPersistentFoam::PreviousCenterParameter,
		FLinearColor(PreviousCenter.X, PreviousCenter.Y, 0.0f, 0.0f));
	FoamUpdateMID->SetVectorParameterValue(
		SWPersistentFoam::CurrentCenterParameter,
		FLinearColor(CurrentCenter.X, CurrentCenter.Y, 0.0f, 0.0f));
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::FieldSizeParameter, FieldWorldSizeCm);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::ResolutionParameter, static_cast<float>(Resolution));
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::DeltaSecondsParameter, SafeDeltaSeconds);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::LifetimeParameter, FoamLifetimeSeconds);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::SourceRateParameter, SourceRate);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::CrestStartParameter, CrestStartCm);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::CrestEndParameter, CrestEndCm);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::SlopeStartParameter, SlopeStart);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::SlopeEndParameter, SlopeEnd);
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::AdvectionScaleParameter, AdvectionScale);
	FoamUpdateMID->SetVectorParameterValue(
		SWPersistentFoam::CpuWaveFieldCenterParameter,
		FLinearColor(CpuWaveFieldCenter.X, CpuWaveFieldCenter.Y, 0.0f, 0.0f));
	FoamUpdateMID->SetScalarParameterValue(SWPersistentFoam::MaxEncodedVelocityParameter, MaxEncodedVelocityCmPerSecond);

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, WriteState, FoamUpdateMID);
	bLatestStateIsA = !bLatestStateIsA;
	PushStateToWaterMaterial(WriteState);

	if (bLogInitialStateStatistics && !bInitialStateStatisticsLogged)
	{
		InitialStateStatisticsElapsedSeconds += SafeDeltaSeconds;
		if (InitialStateStatisticsElapsedSeconds >= 2.0f)
		{
			LogInitialStateStatistics(WriteState);
			bInitialStateStatisticsLogged = true;
		}
	}
}

void ASWPersistentFoamField::LogInitialStateStatistics(UTextureRenderTarget2D* State)
{
	if (!IsValid(State) || !State->GameThread_GetRenderTargetResource())
	{
		UE_LOG(LogSWPersistentFoam, Warning, TEXT("State statistics unavailable: render target is invalid."));
		return;
	}

	TArray<FFloat16Color> Pixels;
	if (!State->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels) || Pixels.IsEmpty())
	{
		UE_LOG(LogSWPersistentFoam, Warning, TEXT("State statistics readback failed."));
		return;
	}

	const int32 Step = FMath::Max(Pixels.Num() / 4096, 1);
	float Minimum = TNumericLimits<float>::Max();
	float Maximum = 0.0f;
	double Sum = 0.0;
	int32 SampleCount = 0;
	int32 CoveredSampleCount = 0;
	for (int32 Index = 0; Index < Pixels.Num(); Index += Step)
	{
		const float Density = FMath::Clamp(Pixels[Index].R.GetFloat(), 0.0f, 1.0f);
		Minimum = FMath::Min(Minimum, Density);
		Maximum = FMath::Max(Maximum, Density);
		Sum += Density;
		CoveredSampleCount += Density >= 0.01f ? 1 : 0;
		++SampleCount;
	}

	const UWaterBodyComponent* WaterComponent = TargetWaterBody ? TargetWaterBody->GetWaterBodyComponent() : nullptr;
	UE_LOG(
		LogSWPersistentFoam,
		Display,
		TEXT("State statistics: Samples=%d Min=%.5f Mean=%.5f Max=%.5f Coverage=%.2f%% WaterIndex=%d WaterMID=%s"),
		SampleCount,
		Minimum,
		SampleCount > 0 ? static_cast<float>(Sum / SampleCount) : 0.0f,
		Maximum,
		SampleCount > 0 ? 100.0f * static_cast<float>(CoveredSampleCount) / SampleCount : 0.0f,
		WaterComponent ? WaterComponent->GetWaterBodyIndex() : INDEX_NONE,
		*GetNameSafe(WaterMID));
}

void ASWPersistentFoamField::PushStateToWaterMaterial(UTextureRenderTarget2D* State)
{
	if (!IsValid(WaterMID) || !IsValid(State))
	{
		return;
	}

	WaterMID->SetTextureParameterValue(SWPersistentFoam::OutputStateParameter, State);
	WaterMID->SetVectorParameterValue(
		SWPersistentFoam::CurrentCenterParameter,
		FLinearColor(CurrentCenter.X, CurrentCenter.Y, 0.0f, 0.0f));
	WaterMID->SetScalarParameterValue(SWPersistentFoam::FieldSizeParameter, FieldWorldSizeCm);
}

UTextureRenderTarget2D* ASWPersistentFoamField::GetCurrentFoamState() const
{
	return bLatestStateIsA ? FoamStateA : FoamStateB;
}

void ASWPersistentFoamField::ResetFoamState()
{
	if (FoamStateA)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FoamStateA, FLinearColor::Transparent);
	}
	if (FoamStateB)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FoamStateB, FLinearColor::Transparent);
	}
	bLatestStateIsA = true;
	PushStateToWaterMaterial(FoamStateA);
}
