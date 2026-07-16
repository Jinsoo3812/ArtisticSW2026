#include "Profiling/SWLevelProfileController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/MiscTrace.h"

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
	WarmupSeconds = FMath::Max(0.0f, WarmupSeconds);
	CaptureFrames = FMath::Max(1, CaptureFrames);
	BeginWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] Ready Map=%s NetMode=%d Warmup=%.2f Frames=%d EnemyLimit=%d DisableEnemyOverlaps=%s DisableEnemyRootOverlaps=%s AutoQuit=%s"),
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("NoWorld"),
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		WarmupSeconds,
		CaptureFrames,
		EnemyShipLimit,
		bDisableEnemyOverlaps ? TEXT("true") : TEXT("false"),
		bDisableEnemyRootOverlaps ? TEXT("true") : TEXT("false"),
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
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SW-LEVEL-PROFILE] Scenario EnemyFound=%d EnemyLimit=%d DisableEnemyOverlaps=%s DisableEnemyRootOverlaps=%s DisabledComponents=%d"),
		EnemyShips.Num(),
		EnemyShipLimit,
		bDisableEnemyOverlaps ? TEXT("true") : TEXT("false"),
		bDisableEnemyRootOverlaps ? TEXT("true") : TEXT("false"),
		DisabledOverlapComponents);
}
