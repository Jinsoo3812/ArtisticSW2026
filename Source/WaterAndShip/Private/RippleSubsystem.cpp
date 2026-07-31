#include "RippleSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Profiling/SWRippleProfileController.h"
#include "Profiling/SWLevelProfileController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/Texture2DResource.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "Water/SWRippleStateSubsystem.h"
#include "Water/SWRippleProfile.h"
#include "Water/SWRippleSettings.h"
#include "Water/SWRippleTypes.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

namespace
{
	const TCHAR* GetRippleNetMode(const UWorld* World)
	{
		if (!World) return TEXT("NoWorld");
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

URippleSubsystem::URippleSubsystem()
{
}

void URippleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDiagnosticsEnabled = FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics"));
	RippleCapacity = GetDefault<USWRippleSettings>()->GetMaxRippleCount();

	// Dedicated servers keep the authoritative CPU cache in USWRippleStateSubsystem,
	// but never allocate render resources.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	RippleTexture = UTexture2D::CreateTransient(RippleCapacity, 2, PF_A32B32G32R32F);
	if (RippleTexture)
	{
		RippleTexture->SRGB = false;
		RippleTexture->CompressionSettings = TC_VectorDisplacementmap;
		RippleTexture->Filter = TF_Nearest;
		RippleTexture->AddressX = TA_Clamp;
		RippleTexture->AddressY = TA_Clamp;
		RippleTexture->UpdateResource();
	}

	UpdateTexture();
}

void URippleSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			if (AWaterBody* WaterBody = *It)
			{
				WaterBody->OnActorBeginOverlap.RemoveAll(this);
			}
		}
	}

	RippleTexture = nullptr;
	Super::Deinitialize();
}

void URippleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	DiagnosticsStartTime = InWorld.GetTimeSeconds();

	// Gameplay ripple detection is server-authoritative. Clients only consume
	// replicated FSWRippleEvents and render/query their local authenticated cache.
	if (InWorld.GetNetMode() != NM_Client)
	{
		for (TActorIterator<AWaterBody> It(&InWorld); It; ++It)
		{
			if (AWaterBody* WaterBody = *It)
			{
				WaterBody->OnActorBeginOverlap.AddUniqueDynamic(this, &URippleSubsystem::OnWaterBodyActorOverlap);
			}
		}
	}

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-AUTH][%s] BeginPlay DetectionAuthority=%s"),
			GetRippleNetMode(&InWorld),
			InWorld.GetNetMode() != NM_Client ? TEXT("true") : TEXT("false"));
	}

	// The profile map remains a clean copy of KKH_Test. The deterministic driver is
	// transiently spawned only when the explicit profiling command-line flag is set.
	if (FSWRippleProfile::IsEnabled() && InWorld.GetMapName().Contains(TEXT("KKH_Profile_Ripple")))
	{
		bool bAlreadySpawned = false;
		for (TActorIterator<ASWRippleProfileController> It(&InWorld); It; ++It)
		{
			bAlreadySpawned = true;
			break;
		}

		if (!bAlreadySpawned)
		{
			FVector ProfileLocation = FVector::ZeroVector;
			for (TActorIterator<AActor> It(&InWorld); It; ++It)
			{
				if (It->GetClass()->GetName().Contains(TEXT("Storage")))
				{
					ProfileLocation = It->GetActorLocation();
					ProfileLocation.Z = 0.0;
					break;
				}
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = TEXT("SW_Ripple_Profile_Controller");
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.ObjectFlags |= RF_Transient;
			InWorld.SpawnActor<ASWRippleProfileController>(
				ASWRippleProfileController::StaticClass(),
				FTransform(ProfileLocation),
				SpawnParameters);
		}
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SWProfileLevel")))
	{
		FString TargetMap(TEXT("Test_Level"));
		FParse::Value(FCommandLine::Get(), TEXT("SWProfileLevelMap="), TargetMap);
		if (InWorld.GetMapName().Contains(TargetMap))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = TEXT("SW_Level_Profile_Controller");
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.ObjectFlags |= RF_Transient;
			InWorld.SpawnActor<ASWLevelProfileController>(
				ASWLevelProfileController::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		}
	}
}

void URippleSubsystem::OnWaterBodyActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !OtherActor || OtherActor == OverlappedActor)
	{
		return;
	}
	if (FSWRippleProfile::IsEnabled() && World->GetMapName().Contains(TEXT("KKH_Profile_Ripple")))
	{
		return;
	}

	const float DownwardSpeed = -OtherActor->GetVelocity().Z;
	if (DownwardSpeed < MinVelocityThreshold)
	{
		return;
	}

	const FVector ContactLocation = OtherActor->GetActorLocation();
	const float InitialAmplitude = FMath::Clamp(
		DownwardSpeed * AmplitudeMultiplier,
		10.0f,
		MaxInitialAmplitude);
	AddRipple(FVector2D(ContactLocation.X, ContactLocation.Y), InitialAmplitude, DefaultWaveSpeed, DefaultDecayRate, DefaultWaveLength);
}

TStatId URippleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URippleSubsystem, STATGROUP_Tickables);
}

void URippleSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld())
	{
		return;
	}

	TickDiagnostics();
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UpdateTexture();
	BindRippleDataToWaterMaterials();
}

void URippleSubsystem::AddRipple(
	FVector2D Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	bool bIsNearPlayer = false;
	TArray<AActor*> Pawns;
	UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Pawns);
	for (const AActor* Actor : Pawns)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		if (Pawn && Pawn->IsPlayerControlled())
		{
			const FVector Location = Pawn->GetActorLocation();
			if (FVector2D::DistSquared(Origin, FVector2D(Location.X, Location.Y)) <= FMath::Square(MaxGenerationDistance))
			{
				bIsNearPlayer = true;
				break;
			}
		}
	}

	if (!bIsNearPlayer && Pawns.Num() > 0)
	{
		return;
	}

	USWRippleStateSubsystem* StateSubsystem = World->GetSubsystem<USWRippleStateSubsystem>();
	const bool bAccepted = StateSubsystem
		&& StateSubsystem->SubmitAuthoritativeRipple(Origin, InitialAmplitude, WaveSpeed, DecayRate, WaveLength);

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-AUTH][%s] Submit Accepted=%s Origin=%s Amp=%.2f"),
			GetRippleNetMode(World),
			bAccepted ? TEXT("true") : TEXT("false"),
			*Origin.ToString(),
			InitialAmplitude);
	}
}

float URippleSubsystem::GetRippleHeight(const FVector& Location) const
{
	UWorld* World = GetWorld();
	const USWRippleStateSubsystem* StateSubsystem = World
		? World->GetSubsystem<USWRippleStateSubsystem>()
		: nullptr;
	return StateSubsystem ? StateSubsystem->GetRippleHeight(Location, GetServerTime()) : 0.0f;
}

void URippleSubsystem::UpdateTexture()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_UpdateTexture);
	const uint64 ProfileStartCycles = FSWRippleProfile::IsEnabled() ? FPlatformTime::Cycles64() : 0;
	if (!RippleTexture || !GetWorld())
	{
		return;
	}

	const USWRippleStateSubsystem* StateSubsystem = GetWorld()->GetSubsystem<USWRippleStateSubsystem>();
	const uint32 StateRevision = StateSubsystem ? StateSubsystem->GetRevision() : 0;
	if (bHasUploadedStateRevision && LastUploadedStateRevision == StateRevision)
	{
		FSWRippleProfile::RecordTextureUpdate(LastUploadedRippleCount, StateRevision, true);
		FSWRippleProfile::RecordTextureUpdateCycles(FPlatformTime::Cycles64() - ProfileStartCycles);
		return;
	}

	TArray<FLinearColor> PixelData;
	PixelData.SetNumZeroed(RippleCapacity * 2);

	TArray<FSWRippleEvent> ActiveEvents;
	if (StateSubsystem)
	{
		StateSubsystem->GetActiveEventsSnapshot(GetServerTime(), ActiveEvents);
	}
	const int32 Count = FMath::Min(ActiveEvents.Num(), RippleCapacity);
	FSWRippleProfile::RecordTextureUpdate(Count, StateRevision, false);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWRippleEvent& Ripple = ActiveEvents[Index];
		PixelData[Index] = FLinearColor(
			Ripple.Origin.X,
			Ripple.Origin.Y,
			static_cast<float>(Ripple.StartServerTime),
			Ripple.InitialAmplitude);
		PixelData[Index + RippleCapacity] = FLinearColor(
			Ripple.WaveSpeed,
			Ripple.DecayRate,
			Ripple.WaveLength,
			static_cast<float>(Ripple.ExpireServerTime));
	}

	FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(RippleTexture->GetResource());
	const bool bResourceValid = TextureResource != nullptr;
	if (bDiagnosticsEnabled
		&& (Count != DiagnosticsLastUploadedRippleCount || bResourceValid != bDiagnosticsLastTextureResourceValid))
	{
		DiagnosticsLastUploadedRippleCount = Count;
		bDiagnosticsLastTextureResourceValid = bResourceValid;
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-AUTH][%s] Texture Active=%d RevisionResource=%s"),
			GetRippleNetMode(GetWorld()), Count, bResourceValid ? TEXT("true") : TEXT("false"));
	}

	if (TextureResource)
	{
		FSWRippleProfile::RecordTextureUpload(PixelData.Num() * sizeof(FLinearColor));
		ENQUEUE_RENDER_COMMAND(UpdateAuthenticatedRippleTexture)(
			[TextureResource, DataCopy = MoveTemp(PixelData), TextureWidth = RippleCapacity](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_RenderThreadTextureUpload);
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, TextureWidth, 2);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					TextureWidth * sizeof(FLinearColor),
					reinterpret_cast<const uint8*>(DataCopy.GetData()));
			});
		bHasUploadedStateRevision = true;
		LastUploadedStateRevision = StateRevision;
		LastUploadedRippleCount = Count;
	}
	FSWRippleProfile::RecordTextureUpdateCycles(FPlatformTime::Cycles64() - ProfileStartCycles);
}

void URippleSubsystem::BindRippleDataToWaterMaterials()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_BindWaterMaterials);
	const uint64 ProfileStartCycles = FSWRippleProfile::IsEnabled() ? FPlatformTime::Cycles64() : 0;
	if (!GetWorld() || !RippleTexture)
	{
		return;
	}

	static const FName RippleTextureParameterName(TEXT("RippleTex"));
	static const FName ServerTimeParameterName(TEXT("ServerTime"));
	const float ServerTime = static_cast<float>(GetServerTime());
	int32 WaterBodyCount = 0;
	int32 ParameterWriteCount = 0;

	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		++WaterBodyCount;
		if (AWaterBody* WaterBody = *It)
		{
			if (UWaterBodyComponent* WaterComponent = WaterBody->GetWaterBodyComponent())
			{
				if (UMaterialInstanceDynamic* WaterMID = WaterComponent->GetWaterMaterialInstance())
				{
					WaterMID->SetTextureParameterValue(RippleTextureParameterName, RippleTexture);
					WaterMID->SetScalarParameterValue(ServerTimeParameterName, ServerTime);
					ParameterWriteCount += 2;
				}
			}
		}
	}
	FSWRippleProfile::RecordMaterialBind(
		WaterBodyCount,
		ParameterWriteCount,
		FPlatformTime::Cycles64() - ProfileStartCycles);
}

double URippleSubsystem::GetServerTime() const
{
	UWorld* World = GetWorld();
	const USWRippleStateSubsystem* StateSubsystem = World
		? World->GetSubsystem<USWRippleStateSubsystem>()
		: nullptr;
	return StateSubsystem ? StateSubsystem->GetServerTime() : (World ? World->GetTimeSeconds() : 0.0);
}

void URippleSubsystem::TickDiagnostics()
{
	if (!bDiagnosticsEnabled || !GetWorld())
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - DiagnosticsStartTime;
	if (DiagnosticsLastSummaryTime < 0.0f || Elapsed - DiagnosticsLastSummaryTime >= 1.0f)
	{
		DiagnosticsLastSummaryTime = Elapsed;
		const USWRippleStateSubsystem* StateSubsystem = GetWorld()->GetSubsystem<USWRippleStateSubsystem>();
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-AUTH][%s] Summary Events=%d Revision=%u ServerTime=%.3f"),
			GetRippleNetMode(GetWorld()),
			StateSubsystem ? StateSubsystem->GetEventCount() : 0,
			StateSubsystem ? StateSubsystem->GetRevision() : 0,
			GetServerTime());
	}
}
