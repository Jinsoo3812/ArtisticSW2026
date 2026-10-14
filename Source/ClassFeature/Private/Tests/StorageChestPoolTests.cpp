#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "BaseGameplayTags.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageChestPool.h"
#include "Storage/SharedStorageChest.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorageChestPoolLifecycleTest,
	"ArtisticSW.Chest.Pool.LifecycleAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStorageChestPoolLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 0);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StorageChestPoolTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->BeginPlay();
	AStorageChestPool* Pool = World->GetSubsystem<UStorageChestPoolSubsystem>()->GetPool();
	if (!TestNotNull(TEXT("Server creates chest pool"), Pool))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	TestEqual(TEXT("Five Actor baseline per BP class"), Pool->PoolCapacityPerClass, 5);
	AChestSpawnPoint* PointA = World->SpawnActor<AChestSpawnPoint>();
	AChestSpawnPoint* PointB = World->SpawnActor<AChestSpawnPoint>();
	PointA->ConfigureRandomSpawn(EProgressionZone::Mid1, EProgressionChestKind::IslandRandom);
	PointB->ConfigureRandomSpawn(EProgressionZone::Mid2, EProgressionChestKind::IslandRandom);
	AStorageChest* ChestA = PointA->SpawnConfiguredChest(nullptr, 11);
	AStorageChest* ChestB = PointB->SpawnConfiguredChest(nullptr, 22);
	// A disposable world has no GameMode to dispatch Actor BeginPlay for us.
	if (!ChestA->HasActorBegunPlay()) ChestA->DispatchBeginPlay();
	if (!ChestB->HasActorBegunPlay()) ChestB->DispatchBeginPlay();
	FStorageItemEntry Item;
	Item.ItemTag = Item_Id_Material_ShipMaterials_WoodenPlank;
	Item.Count = 1;
	ChestA->ConfigureStorage(4, 2, {Item});
	Item.Count = 6;
	ChestB->ConfigureStorage(7, 3, {Item});
	FFloatProperty* Mass = FindFProperty<FFloatProperty>(AStorageChest::StaticClass(), TEXT("PhysicsMassKg"));
	Mass->SetPropertyValue_InContainer(ChestA, 10.f);
	Mass->SetPropertyValue_InContainer(ChestB, 99.f);
	Pool->Discover();
	const FGuid IdA = PointA->GetPooledChest().StableId;
	const FGuid IdB = PointB->GetPooledChest().StableId;
	TestTrue(TEXT("Independent random land chest eligible"), AStorageChestPool::IsEligible(*PointA, ChestA));
	ChestA->SetLocked(true);
	TestFalse(TEXT("Locked random chest excluded"), AStorageChestPool::IsEligible(*PointA, ChestA));
	ChestA->SetLocked(false);
	PointA->SetEnvironment(EChestEnvironment::ShipDeck);
	TestFalse(TEXT("Deck chest excluded"), AStorageChestPool::IsEligible(*PointA, ChestA));
	PointA->SetEnvironment(EChestEnvironment::Land);
	PointA->ConfigureGuardedSpawn(nullptr, {}, nullptr);
	TestFalse(TEXT("Guarded point excluded even without living guards"), AStorageChestPool::IsEligible(*PointA, ChestA));
	PointA->ConfigureRandomSpawn(EProgressionZone::Mid1, EProgressionChestKind::IslandRandom);

	FString Error;
	AStorageChest* ForeignReserve = World->SpawnActor<ASharedStorageChest>();
	Pool->Park(*ForeignReserve);
	Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Add(ForeignReserve);
	TestTrue(TEXT("Sleep A"), Pool->Sleep(*PointA, Error));
	TestTrue(TEXT("Sleep B"), Pool->Sleep(*PointB, Error));
	TestNull(TEXT("Dormant point releases Actor pointer"), PointA->GetSpawnedActor());
	TestTrue(TEXT("Returned Actor hidden and collision disabled"), ChestA->IsHidden() && !ChestA->GetActorEnableCollision());
	TestTrue(TEXT("Wake B"), Pool->Wake(*PointB, Error));
	TestTrue(TEXT("B reuses A's physical Actor"), PointB->GetSpawnedActor() == ChestA);
	TestTrue(TEXT("A different class is never reused for this chest"), Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Contains(ForeignReserve));
	TestEqual(TEXT("Reassigned Actor uses B identity"), ChestA->GetPoolAssignment().StableId, IdB);
	TestEqual(TEXT("B's per-placement mass restored"), Mass->GetPropertyValue_InContainer(ChestA), 99.f);
	TestEqual(TEXT("B slot count restored"), ChestA->GetStorageComponent()->GetSlotCount(), 7);
	TestEqual(TEXT("B columns restored"), ChestA->GetStorageComponent()->GetStorageColumns(), 3);
	TestEqual(TEXT("B contents restored without reroll"), ChestA->GetStorageComponent()->GetPersistentSlots()[0].Count, 6);
	TestTrue(TEXT("Wake A"), Pool->Wake(*PointA, Error));
	TestTrue(TEXT("A reuses B's physical Actor"), PointA->GetSpawnedActor() == ChestB);
	TestEqual(TEXT("A keeps its original logical identity"), ChestB->GetPoolAssignment().StableId, IdA);
	TestEqual(TEXT("A keeps its partially consumed contents"), ChestB->GetStorageComponent()->GetPersistentSlots()[0].Count, 1);
	TestEqual(TEXT("A configuration does not inherit B mass"), Mass->GetPropertyValue_InContainer(ChestB), 10.f);
	Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Remove(ForeignReserve);
	ForeignReserve->Destroy();
	TArray<FSWRoomDomainPart> ActiveParts;
	TArray<FSWRoomCaptureIssue> ActiveIssues;
	Mass->SetPropertyValue_InContainer(ChestA, 88.f);
	PointB->CaptureRoomDomains(ActiveParts, ActiveIssues);
	FSWRoomLootPointState ActiveSaved;
	TestTrue(TEXT("Capture active logical chest"), FSWRoomStructCodec::Read(ActiveParts[0].Bytes, ActiveSaved));
	TestEqual(TEXT("Active capture retains Actor reference"), ActiveSaved.SpawnedActorId, IdB);
	const auto* SavedMass = ActiveSaved.PooledChest.Properties.FindByPredicate([](const auto& Value)
	{ return Value.Object == TEXT("Actor") && Value.Name == TEXT("PhysicsMassKg"); });
	TestTrue(TEXT("Active snapshot refreshes changed placement settings"), SavedMass && FCString::Atof(*SavedMass->Value) == 88.f);
	Mass->SetPropertyValue_InContainer(ChestA, 12.f);
	TestTrue(TEXT("Restore active point profile"), PointB->RestoreRoomDomain(ActiveParts[0], Error));
	TMap<FGuid, AActor*> ActiveActors;
	ActiveActors.Add(IdB, ChestA);
	TestTrue(TEXT("Restore active chest reference"), PointB->FinalizeRoomRestore(ActiveActors, Error));
	Pool->RestoreCompleted();
	TestEqual(TEXT("Active restore applies saved instance settings"), Mass->GetPropertyValue_InContainer(ChestA), 88.f);

	TestTrue(TEXT("Sleep B for save"), Pool->Sleep(*PointB, Error));
	TArray<FSWRoomDomainPart> Parts;
	TArray<FSWRoomCaptureIssue> Issues;
	PointB->CaptureRoomDomains(Parts, Issues);
	TestEqual(TEXT("Dormant capture has no issues"), Issues.Num(), 0);
	FSWRoomLootPointState Saved;
	TestTrue(TEXT("Read logical chest from point domain"), FSWRoomStructCodec::Read(Parts[0].Bytes, Saved));
	TestFalse(TEXT("Dormant point does not reference nonexistent Actor record"), Saved.SpawnedActorId.IsValid());
	TestEqual(TEXT("Dormant save retains chest ID"), Saved.PooledChest.StableId, IdB);
	PointB->ResetSpawnPoint(true);
	TestTrue(TEXT("Restore dormant point domain"), PointB->RestoreRoomDomain(Parts[0], Error));
	TestTrue(TEXT("Dormant point finalizes without Actor record"), PointB->FinalizeRoomRestore({}, Error));
	Pool->Discover();
	TestTrue(TEXT("Wake restored logical chest"), Pool->Wake(*PointB, Error));
	AStorageChest* Restored = Cast<AStorageChest>(PointB->GetSpawnedActor());
	TestEqual(TEXT("Restored contents unchanged"), Restored->GetStorageComponent()->GetPersistentSlots()[0].Count, 6);
	TestEqual(TEXT("Restore preserves runtime origin"), Restored->FindComponentByClass<USWRoomSnapshotComponent>()->GetCreatorSequence(), Saved.PooledChest.CreatorSequence);
	TestTrue(TEXT("Sleep A for capacity check"), Pool->Sleep(*PointA, Error));
	TestTrue(TEXT("Sleep B for capacity check"), Pool->Sleep(*PointB, Error));
	for (int32 Index = Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Num(); Index < 7; ++Index)
	{
		AStorageChest* Reserve = Pool->Allocate(*PointA, PointA->GetPooledChest());
		Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Add(Reserve);
	}
	AStorageChest* Excess = Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Last();
	Pool->Evaluate();
	TestEqual(TEXT("Excess reserves trimmed to five"), Pool->GetClassPool(FSoftClassPath(AStorageChest::StaticClass())).IdleChests.Num(), 5);
	TestTrue(TEXT("At least one excess Actor really destroyed"), Excess->IsActorBeingDestroyed());
	if (!Error.IsEmpty()) AddError(Error);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorageChestPoolClassBudgetTest,
	"ArtisticSW.Chest.Pool.IndependentClassBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStorageChestPoolClassBudgetTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 0);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StorageChestClassBudgetTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	AStorageChestPool* Pool = World->SpawnActor<AStorageChestPool>();
	// Two native classes exercise the same class keys as the small/large BPs.
	// Shared storage is used only as a different allocation class, not registered
	// as an eligible random chest (shared storage remains excluded in gameplay).
	const FSoftClassPath ClassA(AStorageChest::StaticClass());
	const FSoftClassPath ClassB(ASharedStorageChest::StaticClass());
	Pool->GetClassPool(ClassA);
	Pool->GetClassPool(ClassB);
	AChestSpawnPoint* PointA = World->SpawnActor<AChestSpawnPoint>();
	AChestSpawnPoint* PointB = World->SpawnActor<AChestSpawnPoint>();
	PointA->GetPooledChest().StableId = FGuid::NewGuid();
	PointA->GetPooledChest().ClassPath = ClassA;
	PointB->GetPooledChest().StableId = FGuid::NewGuid();
	PointB->GetPooledChest().ClassPath = ClassB;
	Pool->Points.Add(PointA);
	Pool->Points.Add(PointB);
	auto& First = Pool->GetClassPool(ClassA);
	auto& Second = Pool->GetClassPool(ClassB);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Pool->MaintainCapacity(First, 0);
		Pool->MaintainCapacity(Second, 0);
	}
	TestEqual(TEXT("First class has five reserves"), First.IdleChests.Num(), 5);
	TestEqual(TEXT("Second class also has five reserves"), Second.IdleChests.Num(), 5);
	AStorageChest* SecondReserve = Second.IdleChests[0];
	AStorageChest* FirstReserve = First.IdleChests[0];
	Pool->MaintainCapacity(First, 6);
	TestEqual(TEXT("Six active first-class chests require no first-class reserves"), First.IdleChests.Num(), 0);
	TestTrue(TEXT("First-class excess reserves really removed"), FirstReserve->IsActorBeingDestroyed());
	TestEqual(TEXT("First-class demand does not consume second-class capacity"), Second.IdleChests.Num(), 5);
	TestFalse(TEXT("Second-class reserve survives first-class demand"), SecondReserve->IsActorBeingDestroyed());
	// Simulate returning seven Actors after demand exceeded this class's baseline.
	for (int32 Index = 0; Index < 7; ++Index)
		First.IdleChests.Add(Pool->Allocate(*PointA, PointA->GetPooledChest()));
	Pool->MaintainCapacity(First, 0);
	TestEqual(TEXT("First class returns to five after demand ends"), First.IdleChests.Num(), 5);
	TestEqual(TEXT("Second class remains at five"), Second.IdleChests.Num(), 5);
	PointA->SetEnvironment(EChestEnvironment::Water);
	Pool->MaintainCapacity(First, 0);
	TestTrue(TEXT("Environment change does not change class pool identity"), First.ClassPath == PointA->GetPooledChest().ClassPath);
	TestEqual(TEXT("Environment does not create a third pool"), Pool->ClassPools.Num(), 2);
	FString Error;
	Pool->PrepareRestore(FSWRoomWorldSnapshot(), Error);
	TestEqual(TEXT("Restore discards all transient class pools"), Pool->ClassPools.Num(), 0);
	TestTrue(TEXT("Restore releases second-class Actor too"), SecondReserve->IsActorBeingDestroyed());
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return !HasAnyErrors();
}

#endif
