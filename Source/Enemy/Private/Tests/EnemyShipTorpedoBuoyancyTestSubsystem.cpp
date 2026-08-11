#include "Tests/EnemyShipTorpedoBuoyancyTestSubsystem.h"

#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"

bool UEnemyShipTorpedoBuoyancyTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Param(FCommandLine::Get(), TEXT("EnemyShipTorpedoBuoyancyTest"))
		&& World
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UEnemyShipTorpedoBuoyancyTestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	WorldBeginTime = InWorld.GetTimeSeconds();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TORPEDO-TEST][BEGIN] NetMode=%d Authority=%s Map=%s"),
		static_cast<int32>(InWorld.GetNetMode()),
		InWorld.GetNetMode() != NM_Client ? TEXT("true") : TEXT("false"),
		*InWorld.GetMapName());
}

void UEnemyShipTorpedoBuoyancyTestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || bTestFinished)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (World->GetNetMode() != NM_Client && !ProbeTorpedo.IsValid() && ProbeAcquiredTime <= 0.0
		&& Now - WorldBeginTime >= 8.0)
	{
		SpawnServerProbe();
	}
	else if (World->GetNetMode() == NM_Client && !ProbeTorpedo.IsValid() && ProbeAcquiredTime <= 0.0)
	{
		FindReplicatedProbe();
	}

	AEnemyShipTorpedo* Torpedo = ProbeTorpedo.Get();
	if (Torpedo)
	{
		const FVector CurrentLocation = Torpedo->GetActorLocation();
		bObservedFlightMovement |= FVector::DistSquared(CurrentLocation, FirstObservedLocation) > FMath::Square(25.0f);
		bObservedWaterEntry |= Torpedo->HasEnteredWaterForDiagnostics();
		bObservedBuoyancyEnabled |= Torpedo->IsBuoyancyEnabledForDiagnostics();
		if (bObservedWaterEntry)
		{
			ObservedMinimumZ = FMath::Min(ObservedMinimumZ, CurrentLocation.Z);
		}
		if (bObservedBuoyancyEnabled)
		{
			ObservedMaximumZAfterBuoyancy = FMath::Max(ObservedMaximumZAfterBuoyancy, CurrentLocation.Z);
		}
		LastObservedLocation = CurrentLocation;
	}

	if (ProbeAcquiredTime > 0.0 && Now - ProbeAcquiredTime >= 7.0)
	{
		FinishTest();
	}
	else if (Now - WorldBeginTime >= 24.0)
	{
		FinishTest();
	}
}

TStatId UEnemyShipTorpedoBuoyancyTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyShipTorpedoBuoyancyTestSubsystem, STATGROUP_Tickables);
}

bool UEnemyShipTorpedoBuoyancyTestSubsystem::FindWaterTestPoint(FVector& OutSurfaceLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const EWaterBodyQueryFlags QueryFlags =
		EWaterBodyQueryFlags::ComputeLocation
		| EWaterBodyQueryFlags::ComputeDepth
		| EWaterBodyQueryFlags::ComputeVelocity
		| EWaterBodyQueryFlags::IncludeWaves;
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		UWaterBodyComponent* WaterComponent = It->GetWaterBodyComponent();
		if (!WaterComponent)
		{
			continue;
		}
		const FVector Candidate = It->GetActorLocation();
		const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Query =
			WaterComponent->TryQueryWaterInfoClosestToWorldLocation(Candidate, QueryFlags);
		if (Query.HasValue())
		{
			OutSurfaceLocation = Query.GetValue().GetWaterSurfaceLocation();
			return true;
		}
	}
	return false;
}

void UEnemyShipTorpedoBuoyancyTestSubsystem::SpawnServerProbe()
{
	UWorld* World = GetWorld();
	FVector WaterSurface;
	if (!World || !FindWaterTestPoint(WaterSurface))
	{
		UE_LOG(LogTemp, Error, TEXT("[TORPEDO-TEST][SPAWN_FAILED] No queryable WaterBody was found."));
		ProbeAcquiredTime = World ? World->GetTimeSeconds() : 1.0;
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ProbeSourceShip = World->SpawnActor<AEnemyShip>(
		AEnemyShip::StaticClass(),
		WaterSurface + FVector(-500.0f, 0.0f, 800.0f),
		FRotator::ZeroRotator,
		SpawnParameters);

	UClass* TorpedoClass = StaticLoadClass(
		AEnemyShipTorpedo::StaticClass(),
		nullptr,
		TEXT("/Game/New/Enemy_Ship/Blueprints/BP_ES_Torpedo.BP_ES_Torpedo_C"));
	if (!TorpedoClass)
	{
		TorpedoClass = AEnemyShipTorpedo::StaticClass();
	}

	const FVector LaunchLocation = WaterSurface + FVector(0.0f, 0.0f, 500.0f);
	const FVector LaunchDirection = FVector(1.0f, 0.0f, -0.65f).GetSafeNormal();
	ProbeTorpedo = World->SpawnActor<AEnemyShipTorpedo>(
		TorpedoClass,
		LaunchLocation,
		LaunchDirection.Rotation(),
		SpawnParameters);
	if (!ProbeTorpedo.IsValid() || !ProbeSourceShip.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[TORPEDO-TEST][SPAWN_FAILED] Source or torpedo actor failed to spawn."));
		ProbeAcquiredTime = World->GetTimeSeconds();
		return;
	}

	ProbeTorpedo->InitializeTorpedo(ProbeSourceShip.Get(), nullptr, 0.0f, 1200.0f, 10.0f);
	ProbeAcquiredTime = World->GetTimeSeconds();
	FirstObservedLocation = ProbeTorpedo->GetActorLocation();
	LastObservedLocation = FirstObservedLocation;
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TORPEDO-TEST][SPAWNED] Role=SERVER Class=%s Torpedo=%s Surface=%s Launch=%s Velocity=%s"),
		*GetNameSafe(TorpedoClass),
		*GetNameSafe(ProbeTorpedo.Get()),
		*WaterSurface.ToCompactString(),
		*LaunchLocation.ToCompactString(),
		*(LaunchDirection * 1200.0f).ToCompactString());
}

void UEnemyShipTorpedoBuoyancyTestSubsystem::FindReplicatedProbe()
{
	for (TActorIterator<AEnemyShipTorpedo> It(GetWorld()); It; ++It)
	{
		ProbeTorpedo = *It;
		ProbeAcquiredTime = GetWorld()->GetTimeSeconds();
		FirstObservedLocation = It->GetActorLocation();
		LastObservedLocation = FirstObservedLocation;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TORPEDO-TEST][CLIENT_ACQUIRED] Torpedo=%s Location=%s"),
			*It->GetName(),
			*FirstObservedLocation.ToCompactString());
		return;
	}
}

void UEnemyShipTorpedoBuoyancyTestSubsystem::FinishTest()
{
	bTestFinished = true;
	const AEnemyShipTorpedo* Torpedo = ProbeTorpedo.Get();
	const float EntryZ = Torpedo ? Torpedo->GetWaterEntryZForDiagnostics() : 0.0f;
	const float MinimumZ = ObservedMinimumZ < TNumericLimits<float>::Max()
		? ObservedMinimumZ
		: EntryZ;
	const float MaximumZ = ObservedMaximumZAfterBuoyancy > -TNumericLimits<float>::Max()
		? ObservedMaximumZAfterBuoyancy
		: MinimumZ;
	const float SinkDistance = EntryZ - MinimumZ;
	const float RiseDistance = MaximumZ - MinimumZ;
	const bool bPassed = bObservedFlightMovement
		&& bObservedWaterEntry
		&& bObservedBuoyancyEnabled
		&& SinkDistance >= 3.0f
		&& RiseDistance >= 3.0f;
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TORPEDO-TEST][RESULT] Role=%s Result=%s FlightMoved=%s WaterEntered=%s BuoyancyEnabled=%s EntryZ=%.2f MinZ=%.2f MaxZ=%.2f Sink=%.2f Rise=%.2f First=%s Last=%s"),
		GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("CLIENT") : TEXT("SERVER"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"),
		bObservedFlightMovement ? TEXT("true") : TEXT("false"),
		bObservedWaterEntry ? TEXT("true") : TEXT("false"),
		bObservedBuoyancyEnabled ? TEXT("true") : TEXT("false"),
		EntryZ,
		MinimumZ,
		MaximumZ,
		SinkDistance,
		RiseDistance,
		*FirstObservedLocation.ToCompactString(),
		*LastObservedLocation.ToCompactString());

	if (FParse::Param(FCommandLine::Get(), TEXT("EnemyShipTorpedoBuoyancyTestAutoQuit")))
	{
		FPlatformMisc::RequestExit(false);
	}
}
