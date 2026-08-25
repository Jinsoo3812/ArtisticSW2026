#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Task/BTT_MoveToDeckWaypoint.h"
#include "Task/BTT_SelectDeckWaypoint.h"
#include "Task/BTT_WaitAtDeckWaypoint.h"

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckEnemyMVPDefaultsTest,
	"ArtisticSW.Enemy.DeckMVP.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckEnemyMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const UDeckWaypointComponent* WaypointCDO = GetDefault<UDeckWaypointComponent>();
	const ADeckRangedEnemy* EnemyCDO = GetDefault<ADeckRangedEnemy>();
	const UBTT_MoveToDeckWaypoint* MoveTaskCDO = GetDefault<UBTT_MoveToDeckWaypoint>();
	const UBTT_SelectDeckWaypoint* SelectTaskCDO = GetDefault<UBTT_SelectDeckWaypoint>();
	const UBTT_WaitAtDeckWaypoint* WaitTaskCDO = GetDefault<UBTT_WaitAtDeckWaypoint>();
	const AEnemyShip* EnemyShipCDO = GetDefault<AEnemyShip>();
	const UBlueprint* DeckEnemyBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_DeckRangedEnemy.BP_DeckRangedEnemy"));
	const ADeckRangedEnemy* BlueprintEnemyCDO = DeckEnemyBlueprint && DeckEnemyBlueprint->GeneratedClass
		? Cast<ADeckRangedEnemy>(DeckEnemyBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;

	TestNotNull(TEXT("Deck waypoint component exists"), WaypointCDO);
	TestFalse(TEXT("Waypoint has no independent tick"), WaypointCDO->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("Waypoint has no independent replication"), WaypointCDO->GetIsReplicated());
	TestTrue(TEXT("Waypoint is patrol-enabled by default"), WaypointCDO->CanPatrol());
	TestNotNull(TEXT("Pooled deck enemy class exists"), EnemyCDO);
	TestFalse(TEXT("Deck enemy does not force global relevancy"), EnemyCDO->bAlwaysRelevant);
	TestTrue(TEXT("Deck enemy actor remains replicated while active"), EnemyCDO->GetIsReplicated());
	TestTrue(TEXT("Server movement and attachment state replicate to clients"),
		EnemyCDO->IsReplicatingMovement());
	TestFalse(TEXT("Deck enemy lifetime is owned by its pool after death"),
		EnemyCDO->ShouldDestroyAfterDeathFinished());
	TestFalse(TEXT("Deck ranged enemy is a fixed emplacement"), EnemyCDO->CanMoveOnDeck());
	TestNotNull(TEXT("Deck ranged enemy Blueprint exists"), DeckEnemyBlueprint);
	TestNotNull(TEXT("Deck ranged enemy Blueprint has a compatible CDO"), BlueprintEnemyCDO);
	if (BlueprintEnemyCDO)
	{
		TestTrue(TEXT("Deck ranged enemy Blueprint mesh is visible by default"),
			BlueprintEnemyCDO->GetMesh() && BlueprintEnemyCDO->GetMesh()->IsVisible());
	}
	TestNotNull(TEXT("Deck enemy implements the shared live-waypoint movement contract"),
		Cast<IDeckWaypointMovementInterface>(const_cast<ADeckRangedEnemy*>(EnemyCDO)));
	TestNotNull(TEXT("Live-goal move task exists"), MoveTaskCDO);
	TestTrue(TEXT("Live-goal move task owns per-AI timeout memory"),
		MoveTaskCDO->GetInstanceMemorySize() > 0);
	TestNotNull(TEXT("Waypoint selection task exists"), SelectTaskCDO);
	TestNotNull(TEXT("Waypoint wait task exists"), WaitTaskCDO);
	TestFalse(TEXT("New generated points do not become spawn points by default"),
		EnemyShipCDO->DeckWaypointGenerationSettings.bNewPointsCanSpawn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckWaypointFollowsParentTransformTest,
	"ArtisticSW.Enemy.DeckMVP.WaypointFollowsMovingDeck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckWaypointFollowsParentTransformTest::RunTest(const FString& Parameters)
{
	USceneComponent* Deck = NewObject<USceneComponent>();
	UDeckWaypointComponent* Waypoint = NewObject<UDeckWaypointComponent>();
	Waypoint->SetupAttachment(Deck);
	Waypoint->SetRelativeLocation(FVector(200.0f, 50.0f, 0.0f));

	Deck->SetWorldTransform(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(1000.0f, 2000.0f, 300.0f)));
	Deck->UpdateComponentToWorld();
	Waypoint->UpdateComponentToWorld();
	const FVector FirstExpected = Deck->GetComponentTransform().TransformPosition(Waypoint->GetRelativeLocation());
	TestTrue(TEXT("Waypoint derives its first world point from the deck"),
		Waypoint->GetComponentLocation().Equals(FirstExpected, 0.1f));

	Deck->SetWorldTransform(FTransform(FRotator(8.0f, 130.0f, -4.0f), FVector(1300.0f, 2400.0f, 380.0f)));
	Deck->UpdateComponentToWorld();
	Waypoint->UpdateComponentToWorld();
	const FVector MovedExpected = Deck->GetComponentTransform().TransformPosition(Waypoint->GetRelativeLocation());
	TestTrue(TEXT("Waypoint follows translation, yaw, pitch, and roll without its own tick"),
		Waypoint->GetComponentLocation().Equals(MovedExpected, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckFixedAnchorLifecycleTest,
	"ArtisticSW.Enemy.DeckMVP.FixedAnchorLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckFixedAnchorLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("DeckFixedAnchorTestWorld"));
	if (!TestNotNull(TEXT("Transient fixed-anchor world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
	ADeckRangedEnemy* Enemy = World->SpawnActorDeferred<ADeckRangedEnemy>(
		ADeckRangedEnemy::StaticClass(),
		FTransform::Identity,
		Ship,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Fixed-anchor ship is spawned"), Ship)
		|| !TestNotNull(TEXT("Deferred pooled enemy is allocated"), Enemy)
		|| !TestNotNull(TEXT("Fixed-anchor deck mesh asset is available"), CubeMesh))
	{
		CleanupWorld();
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	UStaticMeshComponent* DeckMesh = Ship->GetShipDeckMesh();
	DeckMesh->SetStaticMesh(CubeMesh);
	DeckMesh->SetRelativeScale3D(FVector(10.0f, 10.0f, 0.1f));
	DeckMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeckMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DeckMesh->UpdateComponentToWorld();
	DeckMesh->RecreatePhysicsState();

	UDeckWaypointComponent* Point = NewObject<UDeckWaypointComponent>(Ship);
	Ship->AddInstanceComponent(Point);
	Point->OnComponentCreated();
	Point->SetupAttachment(DeckMesh);
	Point->InitializeGeneratedWaypoint(101, 0, 0, true, true, true);
	Point->RegisterComponent();
	Point->SetRelativeLocation(FVector(100.0f, 50.0f, 10.0f));
	Ship->InitializeDeckWaypoints();

	Enemy->PrepareForPool();
	Enemy->FinishSpawning(FTransform::Identity);
	Enemy->SetHostShip(Ship);
	Enemy->DeactivateToPool();
	Enemy->GetMesh()->SetVisibility(false, true);

	TestTrue(TEXT("Server activates pooled enemy at a committed deck point"),
		Enemy->ActivateFromPool(Ship, 101, 12345));
	TestEqual(TEXT("Active enemy root attaches directly to ShipDeckMesh"),
		Enemy->GetRootComponent()->GetAttachParent(),
		static_cast<USceneComponent*>(DeckMesh));
	TestEqual(TEXT("Fixed enemy movement mode remains MOVE_None"),
		Enemy->GetCharacterMovement()->MovementMode,
		MOVE_None);
	TestFalse(TEXT("Active anchored enemy is visible"), Enemy->IsHidden());
	TestTrue(TEXT("Activation restores mesh component visibility"),
		Enemy->GetMesh()->IsVisible());

	const FVector BeforeShipMove = Enemy->GetActorLocation();
	Ship->AddActorWorldOffset(FVector(250.0f, -125.0f, 40.0f), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Attached enemy follows ship transform without a floor update"),
		Enemy->GetActorLocation().Equals(BeforeShipMove + FVector(250.0f, -125.0f, 40.0f), 0.1f));

	Enemy->DeactivateToPool();
	TestNull(TEXT("Inactive enemy detaches from ShipDeckMesh"),
		Enemy->GetRootComponent()->GetAttachParent());
	TestTrue(TEXT("Inactive pooled enemy is hidden"), Enemy->IsHidden());
	TestEqual(TEXT("Inactive capsule collision is disabled"),
		Enemy->GetCapsuleComponent()->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Inactive pooled enemy becomes fully dormant"),
		Enemy->NetDormancy.GetValue(),
		DORM_DormantAll);

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckPointReservationLifecycleTest,
	"ArtisticSW.Enemy.DeckMVP.PointReservationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckPointReservationLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("DeckPointReservationTestWorld"));
	if (!TestNotNull(TEXT("Transient reservation world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
	AActor* FirstActor = World->SpawnActor<AActor>();
	AActor* SecondActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Reservation ship is spawned"), Ship)
		|| !TestNotNull(TEXT("First deck occupant is spawned"), FirstActor)
		|| !TestNotNull(TEXT("Second deck occupant is spawned"), SecondActor))
	{
		CleanupWorld();
		return false;
	}

	auto AddPoint = [Ship](int32 PointId)
	{
		UDeckWaypointComponent* Point = NewObject<UDeckWaypointComponent>(Ship);
		Ship->AddInstanceComponent(Point);
		Point->OnComponentCreated();
		Point->SetupAttachment(Ship->GetShipDeckMesh());
		Point->InitializeGeneratedWaypoint(PointId, PointId, 0, true, true, true);
		Point->RegisterComponent();
		return Point;
	};
	AddPoint(101);
	AddPoint(102);
	Ship->InitializeDeckWaypoints();

	TestTrue(TEXT("First actor occupies its initial point"),
		Ship->TryOccupyDeckPoint(101, FirstActor));
	TestFalse(TEXT("Occupied point is unavailable to another actor"),
		Ship->IsDeckPointAvailable(101, SecondActor));
	FDeckPointReservation BlockedReservation;
	TestFalse(TEXT("Another actor cannot reserve an occupied point"),
		Ship->TryReserveDeckPoint(101, SecondActor, BlockedReservation));

	FDeckPointReservation FirstReservation;
	TestTrue(TEXT("First actor reserves an empty destination"),
		Ship->TryReserveDeckPoint(102, FirstActor, FirstReservation));
	FDeckPointReservation RacingReservation;
	TestFalse(TEXT("A competing server request cannot reserve the same destination"),
		Ship->TryReserveDeckPoint(102, SecondActor, RacingReservation));
	TestTrue(TEXT("The matching token commits the destination"),
		Ship->CommitDeckPointReservation(FirstReservation, FirstActor));
	Ship->ReleaseDeckPointOccupancy(101, FirstActor);
	Ship->ReleaseDeckPointOccupancy(102, FirstActor);
	TestTrue(TEXT("Released destination becomes available"),
		Ship->IsDeckPointAvailable(102, SecondActor));

	FDeckPointReservation StaleReservation;
	TestTrue(TEXT("Second actor can reserve the released point"),
		Ship->TryReserveDeckPoint(102, SecondActor, StaleReservation));
	FDeckPointReservation StaleCopy = StaleReservation;
	Ship->ReleaseDeckPointReservation(StaleReservation);
	FDeckPointReservation CurrentReservation;
	TestTrue(TEXT("A new request receives a replacement token"),
		Ship->TryReserveDeckPoint(102, FirstActor, CurrentReservation));
	TestFalse(TEXT("A released stale token cannot commit a later reservation"),
		Ship->CommitDeckPointReservation(StaleCopy, SecondActor));
	TestTrue(TEXT("The replacement token still commits successfully"),
		Ship->CommitDeckPointReservation(CurrentReservation, FirstActor));
	Ship->ReleaseAllDeckPointsFor(FirstActor);
	TestTrue(TEXT("Actor cleanup releases every occupied and reserved point"),
		Ship->IsDeckPointAvailable(102, SecondActor));

	CleanupWorld();
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckWaypointMeshGenerationAuthoringTest,
	"ArtisticSW.Enemy.DeckMVP.MeshGenerationPreservesAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckWaypointMeshGenerationAuthoringTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("DeckWaypointGenerationTestWorld"));
	if (!TestNotNull(TEXT("Transient editor world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Enemy ship is spawned"), Ship)
		|| !TestNotNull(TEXT("Engine cube mesh is available"), CubeMesh))
	{
		CleanupWorld();
		return false;
	}

	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	UStaticMeshComponent* DeckMesh = Ship->GetShipDeckMesh();
	DeckMesh->SetStaticMesh(CubeMesh);
	DeckMesh->SetRelativeScale3D(FVector(10.0f, 10.0f, 0.1f));
	DeckMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeckMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DeckMesh->UpdateComponentToWorld();
	DeckMesh->RecreatePhysicsState();
	Ship->DeckWaypointGenerationSettings.GridSpacing = 250.0f;
	Ship->DeckWaypointGenerationSettings.EdgeClearance = 60.0f;
	Ship->GenerateDeckWaypointsFromDeckMesh();

	TArray<UDeckWaypointComponent*> Waypoints;
	Ship->GetComponents<UDeckWaypointComponent>(Waypoints);
	UDeckWaypointComponent* FirstGenerated = nullptr;
	for (UDeckWaypointComponent* Waypoint : Waypoints)
	{
		if (IsValid(Waypoint) && Waypoint->WasGeneratedFromDeckMesh())
		{
			FirstGenerated = Waypoint;
			break;
		}
	}
	if (!TestNotNull(TEXT("Mesh sampling creates at least one editable generated component"), FirstGenerated))
	{
		CleanupWorld();
		return false;
	}
	TestFalse(TEXT("A newly generated point has Can Spawn disabled"), FirstGenerated->CanSpawnEnemy());

	const int32 PreservedId = FirstGenerated->GetWaypointId();
	const int32 PreservedGridX = FirstGenerated->GetGeneratedGridX();
	const int32 PreservedGridY = FirstGenerated->GetGeneratedGridY();
	FirstGenerated->InitializeGeneratedWaypoint(
		PreservedId, PreservedGridX, PreservedGridY, false, false, false);
	Ship->GenerateDeckWaypointsFromDeckMesh();
	TestFalse(TEXT("Regeneration preserves a designer's combat exclusion"), FirstGenerated->CanUseInCombat());
	TestFalse(TEXT("Regeneration preserves a designer's patrol exclusion"), FirstGenerated->CanPatrol());
	TestFalse(TEXT("Regeneration preserves a designer's spawn exclusion"), FirstGenerated->CanSpawnEnemy());
	TestEqual(TEXT("An excluded point is visualized in red"), FirstGenerated->ShapeColor, FColor(220, 45, 45));

	UDeckWaypointComponent* ManualWaypoint = NewObject<UDeckWaypointComponent>(Ship);
	Ship->AddInstanceComponent(ManualWaypoint);
	ManualWaypoint->OnComponentCreated();
	ManualWaypoint->SetupAttachment(DeckMesh);
	ManualWaypoint->RegisterComponent();
	Ship->ClearGeneratedDeckWaypoints();
	TestTrue(TEXT("Clearing generated points preserves manual waypoint components"), IsValid(ManualWaypoint));

	Waypoints.Reset();
	Ship->GetComponents<UDeckWaypointComponent>(Waypoints);
	const bool bHasGeneratedPoint = Waypoints.ContainsByPredicate([](const UDeckWaypointComponent* Waypoint)
	{
		return IsValid(Waypoint) && Waypoint->WasGeneratedFromDeckMesh();
	});
	TestFalse(TEXT("Clear removes all mesh-generated waypoint components"), bHasGeneratedPoint);
	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckWaypointBlueprintAssetGenerationTest,
	"ArtisticSW.Enemy.DeckMVP.BlueprintAssetGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckWaypointBlueprintAssetGenerationTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AEnemyShip::StaticClass(),
		GetTransientPackage(),
		TEXT("BP_DeckWaypointGenerationTest"),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		TEXT("DeckWaypointBlueprintAssetGenerationTest"));
	if (!TestNotNull(TEXT("Transient EnemyShip Blueprint is created"), Blueprint))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	AEnemyShip* BlueprintCDO = Blueprint->GeneratedClass
		? Cast<AEnemyShip>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Transient Blueprint CDO exists"), BlueprintCDO)
		|| !TestNotNull(TEXT("Engine cube mesh is available for Blueprint generation"), CubeMesh))
	{
		return false;
	}

	BlueprintCDO->GetShipDeckMesh()->SetStaticMesh(CubeMesh);
	BlueprintCDO->GetShipDeckMesh()->SetRelativeScale3D(FVector(10.0f, 10.0f, 0.1f));
	BlueprintCDO->GetShipDeckMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BlueprintCDO->GetShipDeckMesh()->SetCollisionResponseToAllChannels(ECR_Block);
	BlueprintCDO->DeckWaypointGenerationSettings.GridSpacing = 250.0f;
	BlueprintCDO->DeckWaypointGenerationSettings.EdgeClearance = 60.0f;
	BlueprintCDO->GenerateDeckWaypointsFromDeckMesh();

	int32 GeneratedNodeCount = 0;
	bool bAllGeneratedSpawnFlagsAreFalse = true;
	bool bAllGeneratedNodesUseNativeDeckParent = true;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		const UDeckWaypointComponent* WaypointTemplate = Node
			? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
			: nullptr;
		if (!WaypointTemplate || !WaypointTemplate->WasGeneratedFromDeckMesh())
		{
			continue;
		}
		++GeneratedNodeCount;
		bAllGeneratedSpawnFlagsAreFalse &= !WaypointTemplate->CanSpawnEnemy();
		bAllGeneratedNodesUseNativeDeckParent &= Node->bIsParentComponentNative
			&& Node->ParentComponentOrVariableName == FName(TEXT("ShipDeckMesh"));
	}
	TestTrue(TEXT("Generation from the Blueprint CDO writes waypoint nodes into the Blueprint SCS"),
		GeneratedNodeCount > 0);
	TestTrue(TEXT("Blueprint-generated waypoint templates default Can Spawn to false"),
		bAllGeneratedSpawnFlagsAreFalse);
	TestTrue(TEXT("Blueprint-generated waypoint nodes attach to native ShipDeckMesh"),
		bAllGeneratedNodesUseNativeDeckParent);

	BlueprintCDO = Blueprint->GeneratedClass
		? Cast<AEnemyShip>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (TestNotNull(TEXT("Blueprint CDO remains available after generation compile"), BlueprintCDO))
	{
		BlueprintCDO->ClearGeneratedDeckWaypoints();
	}
	const bool bGeneratedNodeRemains = Blueprint->SimpleConstructionScript->GetAllNodes().ContainsByPredicate(
		[](const USCS_Node* Node)
		{
			const UDeckWaypointComponent* WaypointTemplate = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr;
			return WaypointTemplate && WaypointTemplate->WasGeneratedFromDeckMesh();
		});
	TestFalse(TEXT("Blueprint Asset clear removes generated SCS nodes"), bGeneratedNodeRemains);
	return true;
}
#endif

#endif
