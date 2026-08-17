#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

	TestNotNull(TEXT("Deck waypoint component exists"), WaypointCDO);
	TestFalse(TEXT("Waypoint has no independent tick"), WaypointCDO->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("Waypoint has no independent replication"), WaypointCDO->GetIsReplicated());
	TestTrue(TEXT("Waypoint is patrol-enabled by default"), WaypointCDO->CanPatrol());
	TestNotNull(TEXT("Pooled deck enemy class exists"), EnemyCDO);
	TestFalse(TEXT("Deck enemy does not force global relevancy"), EnemyCDO->bAlwaysRelevant);
	TestTrue(TEXT("Deck enemy actor remains replicated while active"), EnemyCDO->GetIsReplicated());
	TestNotNull(TEXT("Live-goal move task exists"), MoveTaskCDO);
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
