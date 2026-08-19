#include "Profiling/EnemyNetworkProfileSubsystem.h"

#include "BaseEnemy.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "TimerManager.h"

namespace EnemyNetworkProfile
{
	constexpr int32 MaxEnemyCount = 128;
	constexpr float MaxAnchorWaitSeconds = 60.0f;
	constexpr TCHAR MeleeClassPath[] =
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_MeleeEnemy.BP_MeleeEnemy_C");
	constexpr TCHAR RangedClassPath[] =
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_RangedEnemy.BP_RangedEnemy_C");
}

bool UEnemyNetworkProfileSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Param(FCommandLine::Get(), TEXT("EnemyNetProfile"))
		&& World
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UEnemyNetworkProfileSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("EnemyNetProfileCount="), EnemyCount);
	FParse::Value(CommandLine, TEXT("EnemyNetProfileSeed="), Seed);
	FParse::Value(CommandLine, TEXT("EnemyNetProfileSpawnDelay="), SpawnDelaySeconds);

	FString Mode(TEXT("Combat"));
	FParse::Value(CommandLine, TEXT("EnemyNetProfileMode="), Mode);
	bCombatMode = !Mode.Equals(TEXT("Idle"), ESearchCase::IgnoreCase);
	EnemyCount = FMath::Clamp(EnemyCount, 0, EnemyNetworkProfile::MaxEnemyCount);
	SpawnDelaySeconds = FMath::Max(0.0f, SpawnDelaySeconds);
	AnchorWaitStartSeconds = InWorld.GetTimeSeconds();

	UE_LOG(LogTemp, Warning,
		TEXT("[ENEMY-NET-PROFILE][BEGIN] Role=%s Map=%s Count=%d Mode=%s Seed=%d"),
		InWorld.GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"),
		*InWorld.GetMapName(), EnemyCount, bCombatMode ? TEXT("Combat") : TEXT("Idle"), Seed);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	if (EnemyCount == 0)
	{
		bScenarioSpawned = true;
		TRACE_BOOKMARK(TEXT("EnemyNetProfile Spawn End Count=0 Mode=%s"),
			bCombatMode ? TEXT("Combat") : TEXT("Idle"));
		return;
	}

	InWorld.GetTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&UEnemyNetworkProfileSubsystem::TrySpawnScenario,
		SpawnDelaySeconds,
		false);
}

void UEnemyNetworkProfileSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
	SpawnedEnemies.Reset();
	Super::Deinitialize();
}

void UEnemyNetworkProfileSubsystem::TrySpawnScenario()
{
	UWorld* World = GetWorld();
	if (!World || bScenarioSpawned || World->GetNetMode() == NM_Client)
	{
		return;
	}

	FVector Anchor = FVector::ZeroVector;
	if (!FindScenarioAnchor(Anchor))
	{
		const float WaitedSeconds = World->GetTimeSeconds() - AnchorWaitStartSeconds;
		if (WaitedSeconds < EnemyNetworkProfile::MaxAnchorWaitSeconds)
		{
			World->GetTimerManager().SetTimer(
				SpawnTimerHandle,
				this,
				&UEnemyNetworkProfileSubsystem::TrySpawnScenario,
				1.0f,
				false);
			return;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[ENEMY-NET-PROFILE][FAILED] No player pawn or PlayerStart after %.1f seconds"),
			WaitedSeconds);
		return;
	}

	const TSubclassOf<ABaseEnemy> MeleeClass = LoadEnemyClass(EnemyNetworkProfile::MeleeClassPath);
	const TSubclassOf<ABaseEnemy> RangedClass = LoadEnemyClass(EnemyNetworkProfile::RangedClassPath);
	if (!MeleeClass || !RangedClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[ENEMY-NET-PROFILE][FAILED] Enemy Blueprint class load failed"));
		return;
	}

	TRACE_BOOKMARK(TEXT("EnemyNetProfile Spawn Begin Count=%d Mode=%s Seed=%d"),
		EnemyCount, bCombatMode ? TEXT("Combat") : TEXT("Idle"), Seed);

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FRandomStream Random(Seed);
	int32 SpawnedCount = 0;
	for (int32 EnemyIndex = 0; EnemyIndex < EnemyCount; ++EnemyIndex)
	{
		FVector SpawnPoint = BuildSpawnPoint(EnemyIndex, Anchor);
		SpawnPoint += FVector(Random.FRandRange(-75.0f, 75.0f), Random.FRandRange(-75.0f, 75.0f), 0.0f);

		if (NavigationSystem)
		{
			FNavLocation ProjectedLocation;
			if (NavigationSystem->ProjectPointToNavigation(
				SpawnPoint, ProjectedLocation, FVector(500.0f, 500.0f, 1000.0f)))
			{
				SpawnPoint = ProjectedLocation.Location;
			}
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Name = FName(*FString::Printf(TEXT("EnemyNetProfile_%03d"), EnemyIndex));
		const TSubclassOf<ABaseEnemy> EnemyClass = EnemyIndex % 2 == 0 ? MeleeClass : RangedClass;
		ABaseEnemy* Enemy = World->SpawnActor<ABaseEnemy>(
			EnemyClass,
			SpawnPoint,
			FRotator(0.0f, (Anchor - SpawnPoint).Rotation().Yaw, 0.0f),
			SpawnParameters);
		if (Enemy)
		{
			Enemy->Tags.AddUnique(TEXT("EnemyNetProfileSpawned"));
			SpawnedEnemies.Add(Enemy);
			++SpawnedCount;
		}
	}

	bScenarioSpawned = true;
	TRACE_BOOKMARK(TEXT("EnemyNetProfile Spawn End Requested=%d Spawned=%d Mode=%s"),
		EnemyCount, SpawnedCount, bCombatMode ? TEXT("Combat") : TEXT("Idle"));
	UE_LOG(LogTemp, Warning,
		TEXT("[ENEMY-NET-PROFILE][SPAWNED] Requested=%d Spawned=%d Anchor=%s Mode=%s"),
		EnemyCount, SpawnedCount, *Anchor.ToCompactString(),
		bCombatMode ? TEXT("Combat") : TEXT("Idle"));
}

bool UEnemyNetworkProfileSubsystem::FindScenarioAnchor(FVector& OutAnchor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		if (PlayerController && PlayerController->GetPawn())
		{
			OutAnchor = PlayerController->GetPawn()->GetActorLocation();
			return true;
		}
	}

	// Dedicated server captures wait for a real client so Combat mode actually drives AI/EQS.
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		OutAnchor = It->GetActorLocation();
		return true;
	}

	return false;
}

FVector UEnemyNetworkProfileSubsystem::BuildSpawnPoint(int32 EnemyIndex, const FVector& Anchor) const
{
	const int32 RingIndex = EnemyIndex / 7;
	const int32 IndexInRing = EnemyIndex % 7;
	const float AngleDegrees = (360.0f / 7.0f) * IndexInRing + RingIndex * 17.0f;
	const float Radius = bCombatMode
		? 700.0f + RingIndex * 250.0f
		: 4000.0f + RingIndex * 350.0f;
	return Anchor + FVector(Radius, 0.0f, 0.0f).RotateAngleAxis(AngleDegrees, FVector::UpVector);
}

TSubclassOf<ABaseEnemy> UEnemyNetworkProfileSubsystem::LoadEnemyClass(const TCHAR* ObjectPath) const
{
	return StaticLoadClass(ABaseEnemy::StaticClass(), nullptr, ObjectPath);
}
