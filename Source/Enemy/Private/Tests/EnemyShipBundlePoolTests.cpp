#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipBundlePool.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyShipBundleBudgetTest,
	"ArtisticSW.Enemy.Ship.Pool.TotalBundleBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipBundleBudgetTest::RunTest(const FString& Parameters)
{
	// Existing quest recipe validation runs when the first disposable world loads item data.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 0);
	// No BeginPlay: exercise real Actor allocation/destruction without combat or level authoring.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipBundleBudgetWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	AEnemyShipBundlePool* Pool = World->SpawnActor<AEnemyShipBundlePool>();
	if (!Pool)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	TestEqual(TEXT("Default total bundle target is twelve"), Pool->BundleCapacity, 12);
	FEnemyShipBundlePlacement QueuedPlacement;
	QueuedPlacement.StableId = FGuid::NewGuid();
	QueuedPlacement.bDormant = true;
	QueuedPlacement.HomeTransform.SetLocation(FVector(100.0, 200.0, 0.0));
	Pool->Placements.Add(QueuedPlacement);
	Pool->EnqueueTransition(QueuedPlacement, EEnemyShipPoolWork::Wake);
	Pool->EnqueueTransition(QueuedPlacement, EEnemyShipPoolWork::Wake);
	TestEqual(TEXT("Duplicate wake is queued once"), Pool->TransitionRequests.Num(), 1);
	QueuedPlacement.HomeTransform.SetLocation(FVector(900.0, 900.0, 0.0));
	TestEqual(TEXT("Queued definition does not alias caller mutation"), Pool->TransitionRequests[0].Definition.HomeTransform.GetLocation(), FVector(100.0, 200.0, 0.0));
	Pool->CancelTransitions(TEXT("TestCapture"));
	TestEqual(TEXT("Capture discards pending transitions"), Pool->TransitionRequests.Num(), 0);
	TestNull(TEXT("Capture leaves no partial Actor"), Pool->TransitionShip.Get());
	Pool->Placements.Reset();
	Pool->BundleCapacity = 8;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
		if (Index < 5) Pool->Placements.AddDefaulted_GetRef().ActiveShip = Ship;
		else Pool->IdleBundles.Add(Ship);
	}
	Pool->MaintainBundleCapacity(false);
	TestEqual(TEXT("Five active bundles"), Pool->CountActiveBundles(), 5);
	TestEqual(TEXT("Only three reserves at five active"), Pool->IdleBundles.Num(), 3);
	while (!Pool->IdleBundles.IsEmpty()) Pool->Placements.AddDefaulted_GetRef().ActiveShip = Pool->IdleBundles.Pop();
	Pool->MaintainBundleCapacity(false);
	TestEqual(TEXT("Eight active bundles"), Pool->CountActiveBundles(), 8);
	TestEqual(TEXT("No extra reserves at eight active"), Pool->IdleBundles.Num(), 0);
	Pool->Placements.AddDefaulted_GetRef().ActiveShip = World->SpawnActor<AEnemyShip>();
	Pool->MaintainBundleCapacity(false);
	TestEqual(TEXT("Active demand may expand to nine"), Pool->CountActiveBundles(), 9);
	TestEqual(TEXT("No reserve while over target"), Pool->IdleBundles.Num(), 0);
	AEnemyShip* FirstReturned = Pool->Placements[0].ActiveShip;
	for (auto& Placement : Pool->Placements)
	{
		Pool->IdleBundles.Add(Placement.ActiveShip);
		Placement.ActiveShip = nullptr;
		Pool->MaintainBundleCapacity(false);
		TestEqual(TEXT("Return keeps exactly eight total bundles"), Pool->CountActiveBundles() + Pool->IdleBundles.Num(), 8);
	}
	TestTrue(TEXT("One excess Actor was actually destroyed"), FirstReturned->IsActorBeingDestroyed());
	TestEqual(TEXT("All nine returned: eight idle remain"), Pool->IdleBundles.Num(), 8);
	Pool->BundleCapacity = 0;
	Pool->MaintainBundleCapacity(false);
	TestEqual(TEXT("Zero budget releases every idle Actor"), Pool->IdleBundles.Num(), 0);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
