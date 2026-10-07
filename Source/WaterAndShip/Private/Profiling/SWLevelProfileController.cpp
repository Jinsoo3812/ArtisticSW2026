#include "Profiling/SWLevelProfileController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RippleSubsystem.h"
#include "UnrealClient.h"

ASWLevelProfileController::ASWLevelProfileController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
}

void ASWLevelProfileController::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("SWProfileLevelWarmup="), WarmupSeconds);
	FParse::Value(CommandLine, TEXT("SWProfileLevelFrames="), CaptureFrames);
	FParse::Value(CommandLine, TEXT("SWProfileEnemyShipLimit="), EnemyShipLimit);
	bAutoQuit = FParse::Param(CommandLine, TEXT("SWProfileLevelAutoQuit"));
	bDisableEnemyOverlaps = FParse::Param(CommandLine, TEXT("SWProfileDisableEnemyOverlaps"));
	bDisableEnemyRootOverlaps = FParse::Param(CommandLine, TEXT("SWProfileDisableEnemyRootOverlaps"));
	bDisableEnemyShipShadows = FParse::Param(CommandLine, TEXT("SWProfileDisableEnemyShipShadows"));
	bProfileGPU = FParse::Param(CommandLine, TEXT("SWProfileGPU"));
	bScreenshot = FParse::Param(CommandLine, TEXT("SWProfileScreenshot"));
	FParse::Value(CommandLine, TEXT("SWProfileScreenshotName="), ScreenshotName);
	bFixedWaterCamera = FParse::Param(CommandLine, TEXT("SWProfileFixedWaterCamera"));
	FParse::Value(CommandLine, TEXT("SWProfileFixedCameraZOffset="), FixedCameraZOffset);
	FParse::Value(CommandLine, TEXT("SWProfileFixedCameraPitch="), FixedCameraPitch);
	bInjectRipple = FParse::Param(CommandLine, TEXT("SWProfileInjectRipple"));
	FParse::Value(CommandLine, TEXT("SWProfileRippleLead="), RippleLeadSeconds);
	FParse::Value(CommandLine, TEXT("SWProfileRippleDistance="), RippleForwardDistance);
	FParse::Value(CommandLine, TEXT("SWProfileRippleAmplitude="), RippleAmplitude);
	WarmupSeconds = FMath::Max(0.0f, WarmupSeconds);
	CaptureFrames = FMath::Max(1, CaptureFrames);
	RippleLeadSeconds = FMath::Clamp(RippleLeadSeconds, 0.05f, FMath::Max(0.05f, WarmupSeconds));
	RippleForwardDistance = FMath::Clamp(RippleForwardDistance, 100.0f, 9000.0f);
	RippleAmplitude = FMath::Clamp(RippleAmplitude, 1.0f, 150.0f);
	BeginWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] Ready Map=%s NetMode=%d Warmup=%.2f Frames=%d EnemyLimit=%d DisableEnemyOverlaps=%s DisableEnemyRootOverlaps=%s DisableEnemyShipShadows=%s AutoQuit=%s"),
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("NoWorld"),
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		WarmupSeconds,
		CaptureFrames,
		EnemyShipLimit,
		bDisableEnemyOverlaps ? TEXT("true") : TEXT("false"),
		bDisableEnemyRootOverlaps ? TEXT("true") : TEXT("false"),
		bDisableEnemyShipShadows ? TEXT("true") : TEXT("false"),
		bAutoQuit ? TEXT("true") : TEXT("false"));
}

void ASWLevelProfileController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bScenarioApplied)
	{
		ApplyProfileScenario();
	}
	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (bInjectRipple && !bRippleInjected
		&& WorldTime - BeginWorldTime >= FMath::Max(0.0f, WarmupSeconds - RippleLeadSeconds))
	{
		InjectProfileRipple();
	}
	FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
	if (!bCaptureRequested && WorldTime - BeginWorldTime >= WarmupSeconds)
	{
		FIntPoint ViewportSize = FIntPoint::ZeroValue;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
		}
		bCaptureRequested = true;
		TRACE_BOOKMARK(TEXT("SW Level Profile Measure Begin Map=%s"), GetWorld() ? *GetWorld()->GetMapName() : TEXT("NoWorld"));
		UE_LOG(LogTemp, Display, TEXT("[SW-LEVEL-PROFILE] CaptureBegin Frames=%d Viewport=%dx%d"), CaptureFrames, ViewportSize.X, ViewportSize.Y);
		if (bProfileGPU && GEngine)
		{
			GEngine->Exec(GetWorld(), TEXT("r.ProfileGPU.ShowUI 0"));
			GEngine->Exec(GetWorld(), TEXT("r.ProfileGPU.Screenshot 0"));
			GEngine->Exec(GetWorld(), TEXT("r.ProfileGPU.Sort 1"));
			GEngine->Exec(GetWorld(), TEXT("r.ProfileGPU.ThresholdPercent 0.1"));
			GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
		}
		if (bScreenshot)
		{
			if (ScreenshotName.IsEmpty())
			{
				ScreenshotName = FPaths::ProjectSavedDir() / TEXT("Screenshots/WindowsEditor/SWLevelProfile.png");
			}
			FScreenshotRequest::RequestScreenshot(ScreenshotName, false, false);
			UE_LOG(LogTemp, Display, TEXT("[SW-LEVEL-PROFILE] ScreenshotRequested File=%s"), *ScreenshotName);
		}
		CaptureProcessAndNetworkStart();
		CsvProfiler->BeginCapture(CaptureFrames);
		return;
	}

	if (!bCaptureRequested)
	{
		return;
	}

	if (CsvProfiler->IsCapturing())
	{
		bObservedCapture = true;
		return;
	}

	if (bObservedCapture)
	{
		bObservedCapture = false;
		TRACE_BOOKMARK(TEXT("SW Level Profile Measure End Map=%s"), GetWorld() ? *GetWorld()->GetMapName() : TEXT("NoWorld"));
		UE_LOG(LogTemp, Display, TEXT("[SW-LEVEL-PROFILE] CaptureEnd"));
		LogProcessAndNetworkEnd();
		SetActorTickEnabled(false);
		if (bAutoQuit)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

void ASWLevelProfileController::InjectProfileRipple()
{
	bRippleInjected = true;
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	URippleSubsystem* RippleSubsystem = World ? World->GetSubsystem<URippleSubsystem>() : nullptr;
	if (!PlayerController || !RippleSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[SW-LEVEL-PROFILE] RippleInjectFailed PlayerController=%s Subsystem=%s"),
			PlayerController ? TEXT("true") : TEXT("false"),
			RippleSubsystem ? TEXT("true") : TEXT("false"));
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Target = ViewLocation + ViewRotation.Vector() * RippleForwardDistance;
	const FVector2D Origin(Target.X, Target.Y);
	RippleSubsystem->AddRipple(Origin, RippleAmplitude, 300.0f, 0.35f, 140.0f);
	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] RippleInjected Origin=%s Amplitude=%.2f Lead=%.2f Distance=%.2f"),
		*Origin.ToString(),
		RippleAmplitude,
		RippleLeadSeconds,
		RippleForwardDistance);
}

void ASWLevelProfileController::CaptureProcessAndNetworkStart()
{
	StartUsedPhysicalBytes = FPlatformMemory::GetStats().UsedPhysical;
	if (const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
	{
		NetworkStartInBytes = NetDriver->InTotalBytes;
		NetworkStartOutBytes = NetDriver->OutTotalBytes;
		NetworkStartInPackets = NetDriver->InTotalPackets;
		NetworkStartOutPackets = NetDriver->OutTotalPackets;
		NetworkStartInBunches = NetDriver->InTotalBunches;
		NetworkStartOutBunches = NetDriver->OutTotalBunches;
	}
}

void ASWLevelProfileController::LogProcessAndNetworkEnd() const
{
	const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	const int64 MemoryDeltaBytes = static_cast<int64>(MemoryStats.UsedPhysical) - static_cast<int64>(StartUsedPhysicalBytes);
	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] MemorySummary UsedPhysicalBytes=%llu DeltaBytes=%lld"),
		MemoryStats.UsedPhysical,
		MemoryDeltaBytes);

	if (const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SW-LEVEL-PROFILE] NetworkSummary InBytes=%u OutBytes=%u InPackets=%u OutPackets=%u InBunches=%u OutBunches=%u"),
			NetDriver->InTotalBytes - NetworkStartInBytes,
			NetDriver->OutTotalBytes - NetworkStartOutBytes,
			NetDriver->InTotalPackets - NetworkStartInPackets,
			NetDriver->OutTotalPackets - NetworkStartOutPackets,
			NetDriver->InTotalBunches - NetworkStartInBunches,
			NetDriver->OutTotalBunches - NetworkStartOutBunches);
	}
}

void ASWLevelProfileController::ApplyProfileScenario()
{
	bScenarioApplied = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bFixedWaterCamera)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			ViewLocation.Z += FixedCameraZOffset;
			ViewRotation.Pitch = FixedCameraPitch;
			ViewRotation.Roll = 0.0f;

			FActorSpawnParameters CameraSpawnParameters;
			CameraSpawnParameters.Name = TEXT("SW_Level_Profile_Fixed_Camera");
			CameraSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CameraSpawnParameters.ObjectFlags |= RF_Transient;
			if (ACameraActor* CameraActor = World->SpawnActor<ACameraActor>(
				ACameraActor::StaticClass(),
				FTransform(ViewRotation, ViewLocation),
				CameraSpawnParameters))
			{
				PlayerController->SetViewTarget(CameraActor);
				UE_LOG(LogTemp, Display,
					TEXT("[SW-LEVEL-PROFILE] FixedCamera Location=%s Rotation=%s"),
					*ViewLocation.ToCompactString(),
					*ViewRotation.ToCompactString());
			}
		}
	}

	TArray<AActor*> EnemyShips;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetClass()->GetName().Contains(TEXT("EnemyShip")))
		{
			EnemyShips.Add(Actor);
		}
	}
	EnemyShips.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});

	int32 DisabledOverlapComponents = 0;
	int32 DisabledShadowComponents = 0;
	for (int32 Index = 0; Index < EnemyShips.Num(); ++Index)
	{
		AActor* EnemyShip = EnemyShips[Index];
		if (EnemyShipLimit >= 0 && Index >= EnemyShipLimit)
		{
			EnemyShip->Destroy();
			continue;
		}

		if (bDisableEnemyOverlaps)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(EnemyShip);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (PrimitiveComponent && PrimitiveComponent->GetGenerateOverlapEvents())
				{
					PrimitiveComponent->SetGenerateOverlapEvents(false);
					++DisabledOverlapComponents;
				}
			}
		}
		else if (bDisableEnemyRootOverlaps)
		{
			if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(EnemyShip->GetRootComponent()))
			{
				if (RootPrimitive->GetGenerateOverlapEvents())
				{
					RootPrimitive->SetGenerateOverlapEvents(false);
					++DisabledOverlapComponents;
				}
			}
		}

		if (bDisableEnemyShipShadows)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(EnemyShip);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (PrimitiveComponent
					&& PrimitiveComponent->GetName() == TEXT("ShipVisualMesh")
					&& PrimitiveComponent->CastShadow)
				{
					PrimitiveComponent->SetCastShadow(false);
					++DisabledShadowComponents;
				}
			}
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] Scenario EnemyFound=%d EnemyLimit=%d DisableEnemyOverlaps=%s DisableEnemyRootOverlaps=%s DisabledOverlapComponents=%d DisableEnemyShipShadows=%s DisabledShadowComponents=%d"),
		EnemyShips.Num(),
		EnemyShipLimit,
		bDisableEnemyOverlaps ? TEXT("true") : TEXT("false"),
		bDisableEnemyRootOverlaps ? TEXT("true") : TEXT("false"),
		DisabledOverlapComponents,
		bDisableEnemyShipShadows ? TEXT("true") : TEXT("false"),
		DisabledShadowComponents);
}
