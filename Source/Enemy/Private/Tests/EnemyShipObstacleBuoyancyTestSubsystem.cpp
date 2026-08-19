#include "Tests/EnemyShipObstacleBuoyancyTestSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"

bool UEnemyShipObstacleBuoyancyTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Param(FCommandLine::Get(), TEXT("EnemyShipObstacleBuoyancyTest"))
		&& World
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UEnemyShipObstacleBuoyancyTestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	WorldBeginTime = InWorld.GetTimeSeconds();
	UE_LOG(LogTemp, Warning, TEXT("[OBSTACLE-BUOYANCY-TEST][BEGIN] Map=%s NetMode=%d"),
		*InWorld.GetMapName(), static_cast<int32>(InWorld.GetNetMode()));
}

void UEnemyShipObstacleBuoyancyTestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || bFinished)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (World->GetNetMode() == NM_Client)
	{
		TickClientCollisionProbe(Now);
		return;
	}
	if (!ProbeObstacle.IsValid() && ProbeSpawnTime <= 0.0 && Now - WorldBeginTime >= 3.0)
	{
		SpawnProbe();
	}

	if (AEnemyShipObstacle* Obstacle = ProbeObstacle.Get())
	{
		const double ProbeAge = Now - ProbeSpawnTime;
		if (!bAppliedHorizontalImpulse && ProbeAge >= 2.0)
		{
			if (UPrimitiveComponent* PhysicsRoot = Cast<UPrimitiveComponent>(Obstacle->GetRootComponent()))
			{
				PhysicsRoot->AddImpulse(FVector(1000000.0f, 600000.0f, 0.0f), NAME_None, false);
				bAppliedHorizontalImpulse = true;
				UE_LOG(LogTemp, Warning, TEXT("[OBSTACLE-BUOYANCY-TEST][HORIZONTAL_IMPULSE] Applied=true"));
			}
		}
		const float CurrentZ = Obstacle->GetActorLocation().Z;
		MaximumObservedXYDrift = FMath::Max(
			MaximumObservedXYDrift,
			FVector2D::Distance(SpawnXY, FVector2D(Obstacle->GetActorLocation())));
		MinimumObservedZ = FMath::Min(MinimumObservedZ, CurrentZ);
		bObservedWaterEntry |= Obstacle->HasEnteredWater();
		bObservedBuoyancy |= Obstacle->IsBuoyancyEnabledForDiagnostics();
		if (bObservedBuoyancy)
		{
			MaximumObservedZAfterBuoyancy = FMath::Max(MaximumObservedZAfterBuoyancy, CurrentZ);
		}
	}

	if (ProbeSpawnTime > 0.0 && Now - ProbeSpawnTime >= 8.0)
	{
		FinishTest();
	}
	else if (Now - WorldBeginTime >= 15.0)
	{
		FinishTest();
	}
}

void UEnemyShipObstacleBuoyancyTestSubsystem::TickClientCollisionProbe(double Now)
{
	UWorld* World = GetWorld();
	if (!World || bFinished)
	{
		return;
	}

	for (TActorIterator<AEnemyShipObstacle> It(World); It; ++It)
	{
		UPrimitiveComponent* RootPhysics = Cast<UPrimitiveComponent>(It->GetRootComponent());
		UBoxComponent* Blocker = It->FindComponentByClass<UBoxComponent>();
		if (!RootPhysics || !Blocker)
		{
			continue;
		}

		const bool bRootIsServerDriven = !RootPhysics->IsSimulatingPhysics();
		const bool bBlockerIsKinematic = !Blocker->IsSimulatingPhysics();
		const bool bBlockerParticipatesInPhysics =
			Blocker->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
		const bool bPassed = bRootIsServerDriven && bBlockerIsKinematic && bBlockerParticipatesInPhysics;
		UE_LOG(LogTemp, Warning,
			TEXT("[OBSTACLE-BUOYANCY-TEST][CLIENT_COLLISION] Result=%s RootSim=%s BlockerSim=%s BlockerCollision=%d Actor=%s"),
			bPassed ? TEXT("PASS") : TEXT("FAIL"),
			RootPhysics->IsSimulatingPhysics() ? TEXT("true") : TEXT("false"),
			Blocker->IsSimulatingPhysics() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(Blocker->GetCollisionEnabled()),
			*It->GetName());
		bFinished = true;
		if (FParse::Param(FCommandLine::Get(), TEXT("EnemyShipObstacleBuoyancyTestAutoQuit")))
		{
			FPlatformMisc::RequestExit(false);
		}
		return;
	}

	if (Now - WorldBeginTime >= 15.0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[OBSTACLE-BUOYANCY-TEST][CLIENT_COLLISION] Result=FAIL No replicated obstacle received."));
		bFinished = true;
		if (FParse::Param(FCommandLine::Get(), TEXT("EnemyShipObstacleBuoyancyTestAutoQuit")))
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

TStatId UEnemyShipObstacleBuoyancyTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyShipObstacleBuoyancyTestSubsystem, STATGROUP_Tickables);
}

bool UEnemyShipObstacleBuoyancyTestSubsystem::FindWaterTestPoint(FVector& OutSurfaceLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeLocation
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
		const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Query =
			WaterComponent->TryQueryWaterInfoClosestToWorldLocation(It->GetActorLocation(), QueryFlags);
		if (Query.HasValue())
		{
			OutSurfaceLocation = Query.GetValue().GetWaterSurfaceLocation();
			return true;
		}
	}
	return false;
}

void UEnemyShipObstacleBuoyancyTestSubsystem::SpawnProbe()
{
	UWorld* World = GetWorld();
	FVector SurfaceLocation;
	if (!World || !FindWaterTestPoint(SurfaceLocation))
	{
		UE_LOG(LogTemp, Error, TEXT("[OBSTACLE-BUOYANCY-TEST][SPAWN_FAILED] No queryable WaterBody."));
		ProbeSpawnTime = World ? World->GetTimeSeconds() : 1.0;
		return;
	}

	UClass* ObstacleClass = StaticLoadClass(
		AEnemyShipObstacle::StaticClass(),
		nullptr,
		TEXT("/Game/New/Enemy_Ship/Blueprints/BP_ES_Obstacle.BP_ES_Obstacle_C"));
	if (!ObstacleClass)
	{
		ObstacleClass = AEnemyShipObstacle::StaticClass();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector SpawnLocation = SurfaceLocation + FVector(0.0f, 0.0f, 600.0f);
	ProbeObstacle = World->SpawnActor<AEnemyShipObstacle>(
		ObstacleClass,
		SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParameters);
	ProbeSpawnTime = World->GetTimeSeconds();
	WaterSurfaceZ = SurfaceLocation.Z;
	SpawnXY = FVector2D(SpawnLocation);
	UE_LOG(LogTemp, Warning,
		TEXT("[OBSTACLE-BUOYANCY-TEST][SPAWNED] Class=%s Surface=%s Spawn=%s CollisionProfile=%s"),
		*GetNameSafe(ObstacleClass),
		*SurfaceLocation.ToCompactString(),
		*SpawnLocation.ToCompactString(),
		ProbeObstacle.IsValid() && ProbeObstacle->GetRootComponent()
			? *CastChecked<UPrimitiveComponent>(ProbeObstacle->GetRootComponent())->GetCollisionProfileName().ToString()
			: TEXT("None"));
}

void UEnemyShipObstacleBuoyancyTestSubsystem::FinishTest()
{
	bFinished = true;
	const AEnemyShipObstacle* Obstacle = ProbeObstacle.Get();
	const float FinalZ = Obstacle ? Obstacle->GetActorLocation().Z : -BIG_NUMBER;
	const float MinimumZ = MinimumObservedZ < TNumericLimits<float>::Max() ? MinimumObservedZ : FinalZ;
	const float MaximumZ = MaximumObservedZAfterBuoyancy > -TNumericLimits<float>::Max()
		? MaximumObservedZAfterBuoyancy
		: MinimumZ;
	const bool bRecoveredNearSurface = FinalZ >= WaterSurfaceZ - 250.0f;
	const bool bStayedHorizontallyAnchored = MaximumObservedXYDrift <= 5.0f;
	const bool bPassed = Obstacle && bObservedWaterEntry && bObservedBuoyancy
		&& bRecoveredNearSurface && bAppliedHorizontalImpulse && bStayedHorizontallyAnchored;
	UE_LOG(LogTemp, Warning,
		TEXT("[OBSTACLE-BUOYANCY-TEST][RESULT] Result=%s WaterEntered=%s BuoyancyEnabled=%s HorizontalImpulse=%s MaxXYDrift=%.2f SurfaceZ=%.2f MinZ=%.2f MaxAfterBuoyancyZ=%.2f FinalZ=%.2f"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"),
		bObservedWaterEntry ? TEXT("true") : TEXT("false"),
		bObservedBuoyancy ? TEXT("true") : TEXT("false"),
		bAppliedHorizontalImpulse ? TEXT("true") : TEXT("false"),
		MaximumObservedXYDrift,
		WaterSurfaceZ,
		MinimumZ,
		MaximumZ,
		FinalZ);

	if (FParse::Param(FCommandLine::Get(), TEXT("EnemyShipObstacleBuoyancyTestAutoQuit")))
	{
		FPlatformMisc::RequestExit(false);
	}
}
