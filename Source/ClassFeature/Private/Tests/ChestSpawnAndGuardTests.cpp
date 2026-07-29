#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseCharacter.h"
#include "BaseGameplayTags.h"
#include "BasePlayerController.h"
#include "Components/BaseHealthComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ItemSpawn/LootSpawnTypes.h"
#include "Ship.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"

namespace ChestSystemTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		explicit FTestWorld(const TCHAR* WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(WorldName));
			if (World)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
			}
		}

		~FTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	UBaseHealthComponent* AddHealthComponent(AActor* Actor, const FName ComponentName)
	{
		UBaseHealthComponent* HealthComponent = NewObject<UBaseHealthComponent>(Actor, ComponentName);
		Actor->AddInstanceComponent(HealthComponent);
		HealthComponent->RegisterComponent();
		return HealthComponent;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataDrivenChestSpawnTest,
	"ArtisticSW.Chest.DataDrivenSpawnGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataDrivenChestSpawnTest::RunTest(const FString& Parameters)
{
	ChestSystemTests::FTestWorld TestWorld(TEXT("DataDrivenChestSpawnTestWorld"));
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}

	UDataTable* LootTable = NewObject<UDataTable>(GetTransientPackage());
	LootTable->RowStruct = FChestInitialLootRow::StaticStruct();

	FChestInitialLootRow LootRow;
	LootRow.ItemTag = Item_Id_Material_ShipMaterials_WoodenPlank;
	LootRow.MinCount = 2;
	LootRow.MaxCount = 2;
	LootRow.Weight = 1.f;
	LootTable->AddRow(TEXT("Wood"), LootRow);

	UChestDefinition* Definition = NewObject<UChestDefinition>(GetTransientPackage());
	Definition->ChestClass = AStorageChest::StaticClass();
	Definition->LootTable = LootTable;
	Definition->RollCount = 1;
	Definition->SlotCount = 4;
	Definition->ColumnCount = 2;

	URandomChestGroup* MidBossGroup = NewObject<URandomChestGroup>(GetTransientPackage());
	MidBossGroup->ChestDefinition = Definition;
	MidBossGroup->SpawnCount = 2;

	URandomChestGroup* FinalBossGroup = NewObject<URandomChestGroup>(GetTransientPackage());
	FinalBossGroup->ChestDefinition = Definition;
	FinalBossGroup->SpawnCount = 1;

	TArray<AChestSpawnPoint*> MidBossPoints;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AChestSpawnPoint* Point = World->SpawnActor<AChestSpawnPoint>();
		Point->ConfigureRandomSpawn(MidBossGroup, static_cast<float>(Index + 1));
		MidBossPoints.Add(Point);
	}

	TArray<AChestSpawnPoint*> FinalBossPoints;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AChestSpawnPoint* Point = World->SpawnActor<AChestSpawnPoint>();
		Point->ConfigureRandomSpawn(FinalBossGroup);
		Point->SetPhysicsAndBuoyancyEnabled(true);
		FinalBossPoints.Add(Point);
	}

	AGlobalLootSpawnManager* Manager = World->SpawnActor<AGlobalLootSpawnManager>();
	Manager->SetInitializeOnBeginPlayForTesting(false);
	Manager->SetSpawnSeedForTesting(12345);

	// Match runtime: data-driven chests are deferred-spawned after the world has begun play.
	World->BeginPlay();
	TestEqual(TEXT("Each random group spawns its configured count"), Manager->InitializeDataDrivenChests(), 3);

	auto CountActivated = [](const TArray<AChestSpawnPoint*>& Points)
	{
		int32 Count = 0;
		for (const AChestSpawnPoint* Point : Points)
		{
			if (Point && Point->IsActivated())
			{
				++Count;
			}
		}
		return Count;
	};

	TestEqual(TEXT("Mid-boss group activates two of three points"), CountActivated(MidBossPoints), 2);
	TestEqual(TEXT("Final-boss group activates one of two points"), CountActivated(FinalBossPoints), 1);

	for (AChestSpawnPoint* Point : FinalBossPoints)
	{
		if (Point->IsActivated())
		{
			AStorageChest* FloatingChest = Cast<AStorageChest>(Point->GetSpawnedActor());
			TestNotNull(TEXT("Floating point owns a storage chest"), FloatingChest);
			if (FloatingChest)
			{
				TestTrue(TEXT("Spawn point enables chest physics and buoyancy"), FloatingChest->IsPhysicsAndBuoyancyEnabled());
			}
		}
	}

	for (AChestSpawnPoint* Point : MidBossPoints)
	{
		if (!Point->IsActivated())
		{
			continue;
		}

		AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
		if (!TestNotNull(TEXT("Activated point owns a storage chest"), Chest))
		{
			continue;
		}

		TestFalse(TEXT("Random chest is unlocked"), Chest->IsLocked());
		TestFalse(TEXT("Data-driven static chest has physics disabled"), Chest->IsPhysicsAndBuoyancyEnabled());
		TestEqual(TEXT("Definition configures slot count"), Chest->GetStorageComponent()->GetSlotCount(), 4);
		TestEqual(TEXT("Definition configures column count"), Chest->GetStorageComponent()->GetStorageColumns(), 2);

		const TArray<FInventorySlot>& Slots = Chest->GetStorageComponent()->GetSlots();
		TestTrue(TEXT("Loot table creates at least one storage slot"), !Slots.IsEmpty());
		if (!Slots.IsEmpty())
		{
			TestEqual(TEXT("Loot table selects the configured item"), Slots[0].ItemTag, Item_Id_Material_ShipMaterials_WoodenPlank.GetTag());
			TestEqual(TEXT("Loot table applies the configured quantity"), Slots[0].Count, 2);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuardedChestUnlockTest,
	"ArtisticSW.Chest.GuardedUnlockAndShipFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardedChestUnlockTest::RunTest(const FString& Parameters)
{
	ChestSystemTests::FTestWorld TestWorld(TEXT("GuardedChestUnlockTestWorld"));
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}

	// Spawn after BeginPlay so this raw transient world dispatches actor BeginPlay
	// exactly as a running level does.
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	ABaseCharacter* GuardA = World->SpawnActor<ABaseCharacter>();
	ABaseCharacter* GuardB = World->SpawnActor<ABaseCharacter>();
	UBaseHealthComponent* GuardAHealth = ChestSystemTests::AddHealthComponent(GuardA, TEXT("GuardAHealth"));
	UBaseHealthComponent* GuardBHealth = ChestSystemTests::AddHealthComponent(GuardB, TEXT("GuardBHealth"));

	AStorageChest* IslandChest = World->SpawnActor<AStorageChest>();
	IslandChest->ConfigureGuarding(true, {GuardA, GuardB}, nullptr);

	ABaseCharacter* ShipGuard = World->SpawnActor<ABaseCharacter>();
	UBaseHealthComponent* ShipGuardHealth = ChestSystemTests::AddHealthComponent(ShipGuard, TEXT("ShipGuardHealth"));
	AShip* OwningShip = World->SpawnActor<AShip>();
	UBaseHealthComponent* ShipHealth = ChestSystemTests::AddHealthComponent(OwningShip, TEXT("ShipHealth"));
	ShipHealth->InitializeWithAbilitySystem(OwningShip->GetAbilitySystemComponent());

	AStorageChest* ShipChest = World->SpawnActor<AStorageChest>();
	ShipChest->ConfigureGuarding(true, {ShipGuard}, OwningShip);

	TestTrue(TEXT("Island guarded chest starts locked"), IslandChest->IsLocked());
	TestEqual(TEXT("Island chest tracks both living guards"), IslandChest->GetAliveGuardCount(), 2);

	GuardAHealth->OnDeathStarted.Broadcast(GuardAHealth);
	TestTrue(TEXT("One remaining guard keeps the chest locked"), IslandChest->IsLocked());
	TestEqual(TEXT("One guard remains"), IslandChest->GetAliveGuardCount(), 1);

	GuardBHealth->OnDeathStarted.Broadcast(GuardBHealth);
	TestFalse(TEXT("The last guard death unlocks the island chest"), IslandChest->IsLocked());
	TestEqual(TEXT("No living guards remain"), IslandChest->GetAliveGuardCount(), 0);

	ABasePlayerController* PlayerController = World->SpawnActor<ABasePlayerController>();
	TestNotNull(TEXT("Storage toggle controller spawns"), PlayerController);
	if (PlayerController)
	{
		PlayerController->OpenStorageFromServer(IslandChest);
		TestTrue(TEXT("First interaction opens the chest"), PlayerController->HasOpenStorage());
		PlayerController->OpenStorageFromServer(IslandChest);
		TestFalse(TEXT("Second interaction closes the chest"), PlayerController->HasOpenStorage());
	}

	TestTrue(TEXT("Ship guarded chest starts locked"), ShipChest->IsLocked());
	TestFalse(TEXT("Ship chest physics is disabled"), ShipChest->IsPhysicsAndBuoyancyEnabled());

	ShipHealth->StartDeath();
	TestTrue(TEXT("Ship death permanently fails the guarded reward"), ShipChest->HasGuardFailed());
	TestTrue(TEXT("Failed ship chest remains locked"), ShipChest->IsLocked());

	ShipGuardHealth->OnDeathStarted.Broadcast(ShipGuardHealth);
	TestTrue(TEXT("Killing guards after ship death cannot unlock the chest"), ShipChest->IsLocked());

	OwningShip->Destroy();
	TestTrue(TEXT("Ship chest is destroyed when the owning ship is actually destroyed"), ShipChest->IsActorBeingDestroyed());

	return true;
}

#endif
