#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/ChildActorComponent.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ItemSpawn/BossChestGuaranteedLootData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ShipAI/EnemyShip.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLvlCYCrewChestAuthoringTest,
	"ArtisticSW.Integration.LvlCY.CrewChestAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLvlCYCrewChestAuthoringTest::RunTest(const FString& Parameters)
{
	// Read the saved package without opening it in the editor, starting play, or saving.
	UPackage* Package = LoadPackage(nullptr, TEXT("/Game/Level/Lvl_CY"), LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (!TestNotNull(TEXT("Saved Lvl_CY world loads read-only"), World)) return false;
	int32 ShipCount = 0;
	int32 BossShipCount = 0;
	TArray<AGlobalLootSpawnManager*> Managers;
	TArray<AEnemyShip*> Ships;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (AEnemyShip* Ship = Cast<AEnemyShip>(Actor)) Ships.Add(Ship);
		if (AGlobalLootSpawnManager* Manager = Cast<AGlobalLootSpawnManager>(Actor)) Managers.Add(Manager);
	}
	TestTrue(TEXT("Lvl_CY contains authored enemy ships"), !Ships.IsEmpty());
	TestEqual(TEXT("Lvl_CY has exactly one global loot manager"), Managers.Num(), 1);
	UBossChestGuaranteedLootData* Loot = nullptr;
	if (Managers.Num() == 1)
	{
		FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(Managers[0]->GetClass(), TEXT("BossGuaranteedLootData"));
		const FSoftObjectPtr& Reference = *Property->ContainerPtrToValuePtr<FSoftObjectPtr>(Managers[0]);
		Loot = Cast<UBossChestGuaranteedLootData>(Reference.LoadSynchronous());
		TestNotNull(TEXT("Lvl_CY manager references guaranteed boss loot data"), Loot);
	}
	for (AEnemyShip* Ship : Ships)
	{
		++ShipCount;
		const FString Label = Ship->GetActorLabel();
		TSet<int32> PointIds;
		TInlineComponentArray<UDeckWaypointComponent*> Waypoints(Ship);
		for (UDeckWaypointComponent* Point : Waypoints)
		{
			TestFalse(Label + TEXT(" has unique waypoint IDs"), PointIds.Contains(Point->GetWaypointId()));
			PointIds.Add(Point->GetWaypointId());
		}
		UDeckEnemySpawnerComponent* Spawner = Ship->GetDeckEnemySpawnerComponent();
		const TArray<FDeckEnemySpawnSlot>& Plan = *FindFProperty<FArrayProperty>(Spawner->GetClass(), TEXT("SpawnPlan"))->ContainerPtrToValuePtr<TArray<FDeckEnemySpawnSlot>>(Spawner);
		for (const FDeckEnemySpawnSlot& Slot : Plan)
		{
			TestNotNull(Label + TEXT(" crew slot has an enemy class"), Slot.EnemyClass.Get());
			TestTrue(Label + TEXT(" crew slot references an existing waypoint"), PointIds.Contains(Slot.SpawnPointId));
			TestTrue(Label + TEXT(" crew slot has a valid stats row"), Slot.StatsRow.DataTable && Slot.StatsRow.DataTable->GetRowMap().Contains(Slot.StatsRow.RowName));
		}
		int32 ChestPointCount = 0;
		TInlineComponentArray<UChildActorComponent*> Children(Ship);
		for (UChildActorComponent* Child : Children)
		{
			if (Child->GetChildActorClass() && Child->GetChildActorClass()->IsChildOf(AChestSpawnPoint::StaticClass())) ++ChestPointCount;
		}
		TestTrue(Label + TEXT(" owns at least one chest spawn point"), ChestPointCount > 0);
		TestEqual(Label + TEXT(" authors guarded ship chests"), Ship->ChestSpawnPointChestSettings.SpawnMode, EChestSpawnMode::Guarded);
		UBossEncounterComponent* Encounter = Ship->GetBossEncounterComponent();
		if (!Encounter->IsEncounterEnabled()) continue;
		++BossShipCount;
		UClass* BossClass = Cast<UClass>(FindFProperty<FClassProperty>(Encounter->GetClass(), TEXT("BossClass"))->GetObjectPropertyValue_InContainer(Encounter));
		const int32 BossPoint = FindFProperty<FIntProperty>(Encounter->GetClass(), TEXT("BossSpawnPointId"))->GetPropertyValue_InContainer(Encounter);
		TestNotNull(Label + TEXT(" enabled encounter has a boss class"), BossClass);
		TestTrue(Label + TEXT(" boss point exists"), PointIds.Contains(BossPoint));
		TArray<FStorageItemEntry> Items;
		TestTrue(Label + TEXT(" actual boss class has exact-class guaranteed loot mapping"), Loot && Loot->FindItemsForExactClass(BossClass, Items));
		TestTrue(Label + TEXT(" boss reward contains required items"), !Items.IsEmpty());
		for (const FStorageItemEntry& Item : Items)
			TestTrue(Label + TEXT(" guaranteed item tag and quantity are valid"), Item.ItemTag.IsValid() && Item.Count > 0);
		AddInfo(FString::Printf(TEXT("Boss ship=%s class=%s guaranteed items=%d"), *Label, *GetPathNameSafe(BossClass), Items.Num()));
	}
	TestTrue(TEXT("Lvl_CY contains enabled boss encounters"), BossShipCount > 0);
	AddInfo(FString::Printf(TEXT("Inspected %d ships, %d enabled boss encounters, %d loot managers. Disabled Final encounter is outside boss spawn coverage."), ShipCount, BossShipCount, Managers.Num()));
	return true;
}

#endif
