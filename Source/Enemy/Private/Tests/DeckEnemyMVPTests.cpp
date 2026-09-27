#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckNavigationComponent.h"
#include "DeckAI/DeckNavigationTypes.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
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
#include "Task/BTT_RangedAttack.h"
#include "Task/BTT_SelectDeckWaypoint.h"
#include "Task/BTT_WaitAtDeckWaypoint.h"

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif

namespace
{
	void CollectDeckBehaviorNodes(const UBTNode* Node, TArray<const UBTNode*>& OutNodes)
	{
		if (!Node)
		{
			return;
		}
		OutNodes.Add(Node);
		if (const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
		{
			for (int32 ChildIndex = 0; ChildIndex < Composite->GetChildrenNum(); ++ChildIndex)
			{
				CollectDeckBehaviorNodes(Composite->GetChildNode(ChildIndex), OutNodes);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckEnemyMVPDefaultsTest,
	"ArtisticSW.Enemy.DeckMVP.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckEnemyMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const UDeckWaypointComponent* WaypointCDO = GetDefault<UDeckWaypointComponent>();
	const ADeckEnemy* EnemyCDO = GetDefault<ADeckEnemy>();
	const UBTT_MoveToDeckWaypoint* MoveTaskCDO = GetDefault<UBTT_MoveToDeckWaypoint>();
	const UBTT_SelectDeckWaypoint* SelectTaskCDO = GetDefault<UBTT_SelectDeckWaypoint>();
	const UBTT_WaitAtDeckWaypoint* WaitTaskCDO = GetDefault<UBTT_WaitAtDeckWaypoint>();
	const AEnemyShip* EnemyShipCDO = GetDefault<AEnemyShip>();
	const UDeckEnemySpawnerComponent* SpawnerCDO = EnemyShipCDO
		? EnemyShipCDO->GetDeckEnemySpawnerComponent()
		: nullptr;
	const UDeckNavigationComponent* NavigationCDO = EnemyShipCDO
		? EnemyShipCDO->GetDeckNavigationComponent()
		: nullptr;
	const UBlueprint* DeckEnemyBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_DeckRangedEnemy.BP_DeckRangedEnemy"));
	const ADeckEnemy* BlueprintEnemyCDO = DeckEnemyBlueprint && DeckEnemyBlueprint->GeneratedClass
		? Cast<ADeckEnemy>(DeckEnemyBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;

	TestNotNull(TEXT("Deck waypoint component exists"), WaypointCDO);
	TestFalse(TEXT("Waypoint has no independent tick"), WaypointCDO->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("Waypoint has no independent replication"), WaypointCDO->GetIsReplicated());
	TestTrue(TEXT("Waypoint is patrol-enabled by default"), WaypointCDO->CanPatrol());
	TestNotNull(TEXT("Pooled deck enemy class exists"), EnemyCDO);
	TestNotNull(TEXT("EnemyShip owns one deck enemy spawner component"), SpawnerCDO);
	TestNotNull(TEXT("EnemyShip owns one deck graph navigation component"), NavigationCDO);
	TestNotNull(TEXT("DeckEnemy owns per-agent route state"),
		EnemyCDO ? EnemyCDO->GetDeckEnemyNavigationComponent() : nullptr);
	if (SpawnerCDO)
	{
		TestFalse(TEXT("Server-only spawn and reservation state is not replicated"),
			SpawnerCDO->GetIsReplicated());
	}
	TestFalse(TEXT("Deck enemy does not force global relevancy"), EnemyCDO->bAlwaysRelevant);
	TestTrue(TEXT("Deck enemy actor remains replicated while active"), EnemyCDO->GetIsReplicated());
	TestTrue(TEXT("Server movement and attachment state replicate to clients"),
		EnemyCDO->IsReplicatingMovement());
	TestFalse(TEXT("Deck enemy lifetime is owned by its pool after death"),
		EnemyCDO->ShouldDestroyAfterDeathFinished());
	TestEqual(TEXT("Deck enemy defaults to ranged combat routing"),
		EnemyCDO->GetDeckCombatRole(), EDeckEnemyCombatRole::Ranged);
	TestNotNull(TEXT("Deck ranged enemy Blueprint exists"), DeckEnemyBlueprint);
	TestNotNull(TEXT("Deck ranged enemy Blueprint has a compatible CDO"), BlueprintEnemyCDO);
	if (BlueprintEnemyCDO)
	{
		TestTrue(TEXT("Deck ranged enemy Blueprint mesh is visible by default"),
			BlueprintEnemyCDO->GetMesh() && BlueprintEnemyCDO->GetMesh()->IsVisible());
	}
	TestNotNull(TEXT("Deck enemy implements the shared live-waypoint movement contract"),
		Cast<IDeckWaypointMovementInterface>(const_cast<ADeckEnemy*>(EnemyCDO)));
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
	FDeckGraphPathfinderTest,
	"ArtisticSW.Enemy.DeckMVP.CombatGraphChoosesReachableLowestCostGoal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckGraphPathfinderTest::RunTest(const FString& Parameters)
{
	TMap<int32, FDeckNavigationNode> Nodes;
	auto AddNode = [&Nodes](int32 Id, const FVector& Location, TArray<int32> Links)
	{
		FDeckNavigationNode& Node = Nodes.Add(Id);
		Node.PointId = Id;
		Node.LocalLocation = Location;
		Node.LinkedPointIds = MoveTemp(Links);
	};
	AddNode(1, FVector(0.0f, 0.0f, 0.0f), { 2, 4 });
	AddNode(2, FVector(100.0f, 0.0f, 0.0f), { 1, 3 });
	AddNode(3, FVector(200.0f, 0.0f, 0.0f), { 2 });
	AddNode(4, FVector(0.0f, 50.0f, 0.0f), { 1 });

	TMap<int32, float> Goals;
	Goals.Add(3, 0.0f);
	Goals.Add(4, 100.0f);
	FDeckNavigationPath Path;
	TestTrue(TEXT("A reachable combat goal is found"),
		FDeckGraphPathfinder::FindLowestCostPathToAny(Nodes, 1, Goals, {}, Path));
	TestEqual(TEXT("Travel cost wins before preferred-range score"), Path.GoalPointId, 4);
	TestTrue(TEXT("Shortest route contains start and one hop"),
		Path.PointIds == TArray<int32>({ 1, 4 }));

	TSet<int32> BlockedPoints = { 4 };
	TestTrue(TEXT("The graph routes around an unavailable goal"),
		FDeckGraphPathfinder::FindLowestCostPathToAny(Nodes, 1, Goals, BlockedPoints, Path));
	TestEqual(TEXT("Blocked short goal falls back to the reachable multi-hop goal"), Path.GoalPointId, 3);
	TestTrue(TEXT("Fallback route preserves every linked hop"),
		Path.PointIds == TArray<int32>({ 1, 2, 3 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckRangedCombatTreeCompositionTest,
	"ArtisticSW.Enemy.DeckMVP.RangedCombatTreeHasAttackAndGraphMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckRangedCombatTreeCompositionTest::RunTest(const FString& Parameters)
{
	const UBehaviorTree* CombatTree = LoadObject<UBehaviorTree>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/DeckRanged/BT_Subtree_DeckRangedEnemy_Combat.BT_Subtree_DeckRangedEnemy_Combat"));
	if (!TestNotNull(TEXT("Deck ranged combat subtree loads"), CombatTree)
		|| !TestNotNull(TEXT("Deck ranged combat subtree has a runtime root"), CombatTree->RootNode.Get()))
	{
		return false;
	}

	TArray<const UBTNode*> Nodes;
	CollectDeckBehaviorNodes(CombatTree->RootNode, Nodes);
	const UBTT_SelectDeckWaypoint* CombatSelect = nullptr;
	bool bHasMove = false;
	bool bHasAttack = false;
	for (const UBTNode* Node : Nodes)
	{
		if (const UBTT_SelectDeckWaypoint* Select = Cast<UBTT_SelectDeckWaypoint>(Node))
		{
			CombatSelect = Select;
		}
		bHasMove |= Node && Node->IsA<UBTT_MoveToDeckWaypoint>();
		bHasAttack |= Node && Node->IsA<UBTT_RangedAttack>();
	}
	TestNotNull(TEXT("Combat subtree plans a deck graph route"), CombatSelect);
	if (CombatSelect)
	{
		TestEqual(TEXT("Route selector is authored in Combat mode"),
			CombatSelect->GetSelectionMode(), EDeckWaypointSelectionMode::Combat);
	}
	TestTrue(TEXT("Combat subtree follows live moving-deck points"), bHasMove);
	TestTrue(TEXT("Combat subtree retains the ranged attack task"), bHasAttack);
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
	ADeckEnemy* Enemy = World->SpawnActorDeferred<ADeckEnemy>(
		ADeckEnemy::StaticClass(),
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
	TestNull(TEXT("Moving deck enemy is not rigidly attached to ShipDeckMesh"),
		Enemy->GetRootComponent()->GetAttachParent());
	TestEqual(TEXT("Active deck enemy restores walking movement"),
		Enemy->GetCharacterMovement()->MovementMode,
		MOVE_Walking);
	TestTrue(TEXT("Active deck enemy enables the live-waypoint movement contract"),
		Enemy->CanMoveOnDeck());
	TestFalse(TEXT("Active anchored enemy is visible"), Enemy->IsHidden());
	TestTrue(TEXT("Activation restores mesh component visibility"),
		Enemy->GetMesh()->IsVisible());

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

	TestTrue(TEXT("First enemy atomically claims a final combat point"),
		Ship->TryClaimDeckCombatPoint(102, FirstActor));
	TestFalse(TEXT("A second enemy cannot race for the same final combat point"),
		Ship->TryClaimDeckCombatPoint(102, SecondActor));
	TestTrue(TEXT("A combat claim does not block next-hop traversal reservations"),
		Ship->TryReserveDeckPoint(102, SecondActor, RacingReservation));
	Ship->ReleaseDeckPointReservation(RacingReservation);
	Ship->ReleaseAllDeckPointsFor(FirstActor);
	TestTrue(TEXT("Owner cleanup releases its final combat point claim"),
		Ship->TryClaimDeckCombatPoint(102, SecondActor));

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckEnemySpawnerCompositionTest,
	"ArtisticSW.Enemy.DeckMVP.SpawnerCompositionAndShipLocalTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckEnemySpawnerCompositionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("DeckSpawnerCompositionTestWorld"));
	if (!TestNotNull(TEXT("Transient spawner world is created"), World))
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
	AShip* TriggerShip = World->SpawnActor<AShip>();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UDeckEnemySpawnerComponent* Spawner = Ship ? Ship->GetDeckEnemySpawnerComponent() : nullptr;
	if (!TestNotNull(TEXT("Enemy ship is spawned"), Ship)
		|| !TestNotNull(TEXT("Trigger ship is spawned"), TriggerShip)
		|| !TestNotNull(TEXT("Deck spawner component exists"), Spawner)
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

	auto AddPoint = [Ship, DeckMesh](int32 PointId, const FVector& RelativeLocation, int32 LinkedId)
	{
		UDeckWaypointComponent* Point = NewObject<UDeckWaypointComponent>(Ship);
		Ship->AddInstanceComponent(Point);
		Point->OnComponentCreated();
		Point->SetupAttachment(DeckMesh);
		Point->InitializeGeneratedWaypoint(PointId, PointId, 0, true, true, true);
		Point->SetLinkedWaypointIdsForAuthoring({ LinkedId });
		Point->RegisterComponent();
		Point->SetRelativeLocation(RelativeLocation);
		return Point;
	};
	UDeckWaypointComponent* FirstPoint = AddPoint(101, FVector(-150.0f, 0.0f, 10.0f), 102);
	AddPoint(102, FVector(150.0f, 0.0f, 10.0f), 101);
	Spawner->InitializeWaypoints();

	constexpr float ProbeHalfHeight = 88.0f;
	FTransform FirstAnchor;
	TestTrue(TEXT("Spawner resolves an authored point before the ship moves"),
		Spawner->ResolveFixedDeckAnchorTransform(101, ProbeHalfHeight, FirstAnchor));
	const FVector FirstAnchorLocal = DeckMesh->GetComponentTransform().InverseTransformPosition(
		FirstAnchor.GetLocation());

	Ship->SetActorLocationAndRotation(
		FVector(1400.0f, -900.0f, 320.0f),
		FRotator(9.0f, 127.0f, -6.0f));
	DeckMesh->UpdateComponentToWorld();
	FirstPoint->UpdateComponentToWorld();
	FTransform MovedAnchor;
	TestTrue(TEXT("Spawner resolves the same point after ship translation and rotation"),
		Spawner->ResolveFixedDeckAnchorTransform(101, ProbeHalfHeight, MovedAnchor));
	const FVector MovedAnchorLocal = DeckMesh->GetComponentTransform().InverseTransformPosition(
		MovedAnchor.GetLocation());
	TestTrue(TEXT("Spawn anchor remains stable in ship-local space"),
		MovedAnchorLocal.Equals(FirstAnchorLocal, 0.1f));

	Spawner->bEnableSpawning = true;
	Spawner->SightActivationDelay = 0.0f;
	Spawner->ActivationInterval = 0.05f;
	Spawner->SpawnPlan.Reset();
	FDeckEnemySpawnSlot& BaseSlot = Spawner->SpawnPlan.AddDefaulted_GetRef();
	BaseSlot.EnemyClass = ADeckEnemy::StaticClass();
	BaseSlot.SpawnPointId = 101;
	FDeckEnemySpawnSlot& RangedSlot = Spawner->SpawnPlan.AddDefaulted_GetRef();
	RangedSlot.EnemyClass = ADeckRangedEnemy::StaticClass();
	RangedSlot.SpawnPointId = 102;
	Spawner->InitializePool();

	TestEqual(TEXT("Composition creates exactly two pooled actors"), Spawner->EnemyPool.Num(), 2);
	int32 BaseClassCount = 0;
	int32 RangedClassCount = 0;
	for (const ADeckEnemy* Enemy : Spawner->EnemyPool)
	{
		BaseClassCount += IsValid(Enemy) && Enemy->GetClass() == ADeckEnemy::StaticClass() ? 1 : 0;
		RangedClassCount += IsValid(Enemy) && Enemy->GetClass() == ADeckRangedEnemy::StaticClass() ? 1 : 0;
	}
	TestEqual(TEXT("Composition preserves the exact base enemy class"), BaseClassCount, 1);
	TestEqual(TEXT("Composition preserves the exact ranged enemy class"), RangedClassCount, 1);

	FDeckEnemySpawnRequest ExternalSummonRequest;
	ExternalSummonRequest.Requester = TriggerShip;
	ExternalSummonRequest.PreferredPointIds.Add(101);
	FDeckPointReservation ExternalReservation;
	TestTrue(TEXT("An external boss-style requester uses the shared spawn reservation path"),
		Spawner->TryReserveEnemySpawnPoint(ExternalSummonRequest, ExternalReservation));
	ADeckEnemy* ExternallySummonedEnemy = nullptr;
	TestTrue(TEXT("The shared reservation activates one pooled enemy"),
		Spawner->ActivateEnemyAtReservation(
			ExternalReservation, nullptr, ExternallySummonedEnemy));
	if (ExternallySummonedEnemy)
	{
		ExternallySummonedEnemy->DeactivateToPool();
	}
	TestTrue(TEXT("Returning a summoned enemy releases its shared point occupancy"),
		Spawner->IsPointAvailable(101, TriggerShip));

	TestTrue(TEXT("Sight request starts the configured deployment"),
		Spawner->RequestDeployment(TriggerShip));
	TestFalse(TEXT("A partially deployed wave is not reported as defeated"),
		Spawner->AreAllDeployedEnemiesDefeated());
	Spawner->DeployNextEnemy();
	TestEqual(TEXT("Both configured entries finish deployment"),
		Spawner->GetDeploymentState(), EDeckEnemyDeploymentState::Completed);
	int32 ActiveCount = 0;
	for (const ADeckEnemy* Enemy : Spawner->EnemyPool)
	{
		ActiveCount += IsValid(Enemy) && Enemy->IsPoolActive() ? 1 : 0;
	}
	TestEqual(TEXT("Both exact-class pool actors become active"), ActiveCount, 2);
	TestEqual(TEXT("Spawner tracks both living deployed enemies"),
		Spawner->GetAliveDeployedEnemyCount(), 2);
	TestFalse(TEXT("Completed deployment rejects duplicate Sight"),
		Spawner->RequestDeployment(TriggerShip));

	Spawner->NotifyEnemyDefeated(Spawner->EnemyPool[0]);
	TestFalse(TEXT("One surviving enemy keeps the ship encounter locked"),
		Spawner->AreAllDeployedEnemiesDefeated());
	Spawner->NotifyEnemyDefeated(Spawner->EnemyPool[1]);
	TestTrue(TEXT("The final owned enemy completes the ship encounter"),
		Spawner->AreAllDeployedEnemiesDefeated());
	TestTrue(TEXT("EnemyShip exposes the same completion state"),
		Ship->AreAllOwnedDeckEnemiesDefeated());
	TestEqual(TEXT("EnemyShip exposes zero surviving owned enemies"),
		Ship->GetAliveOwnedDeckEnemyCount(), 0);

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
