#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "BaseGameplayTags.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "ItemSpawn/BossChestGuaranteedLootData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/NavalAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "StoryFacadeSubsystem.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace ShipCrewChestIntegration
{
	// All actors and campaign state belong to this disposable standalone world.
	// No travel, SaveGame, network connection, or package save is performed.
	struct FFixture
	{
		UGameInstance* Instance = nullptr;
		UWorld* World = nullptr;
		AEnemyShip* Ship = nullptr;
		AShip* PlayerShip = nullptr;
		AGlobalLootSpawnManager* Manager = nullptr;
		TArray<AChestSpawnPoint*> Points;

		FFixture(bool bStartBundleIdle = false)
		{
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->InitializeStandalone(TEXT("ShipCrewChestIsolatedWorld"));
			World = Instance->GetWorld();
			World->CreateAISystem();
			Ship = World->SpawnActor<AEnemyShip>();
			Ship->bEnableDistanceOptimization = false;
			PlayerShip = World->SpawnActor<AShip>(FVector(3000, 0, 0), FRotator::ZeroRotator);
			Manager = World->SpawnActor<AGlobalLootSpawnManager>();
			Manager->SetInitializeOnBeginPlayForTesting(false);
			Ship->BuoyancyRoot->SetSimulatePhysics(false);
			PlayerShip->BuoyancyRoot->SetSimulatePhysics(false);
			PlayerShip->Tags.AddUnique(TEXT("Player"));
			UStaticMeshComponent* Deck = Ship->GetShipDeckMesh();
			Deck->SetRelativeScale3D(FVector::OneVector);
			Deck->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(Ship, TEXT("TestFloor"));
			Ship->AddInstanceComponent(Floor);
			Floor->SetupAttachment(Deck);
			Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->SetRelativeScale3D(FVector(20, 20, .1));
			Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Floor->SetCollisionResponseToAllChannels(ECR_Block);
			Floor->RegisterComponent();
			Floor->RecreatePhysicsState();
			for (int32 Index = 0; Index < 3; ++Index)
			{
				UDeckWaypointComponent* Waypoint = NewObject<UDeckWaypointComponent>(Ship);
				Ship->AddInstanceComponent(Waypoint);
				Waypoint->OnComponentCreated();
				Waypoint->SetupAttachment(Deck);
				Waypoint->SetWaypointIdForAuthoring(101 + Index);
				FindFProperty<FBoolProperty>(Waypoint->GetClass(), TEXT("bCanSpawn"))->SetPropertyValue_InContainer(Waypoint, true);
				Waypoint->RegisterComponent();
				Waypoint->SetRelativeLocation(FVector(-400 + Index * 400, 0, 10));
			}
			FDeckWalkSurfaceSettings Surface;
			Surface.SurfaceId = TEXT("TestDeck");
			Surface.FloorComponentNames = {TEXT("TestFloor")};
			Surface.bTraceComplex = false;
			Surface.MinimumFloorZ = 0;
			Surface.MaximumFloorZ = 20;
			UDeckWalkAreaComponent* Area = Ship->GetDeckWalkAreaComponent();
			*FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("Surfaces"))->ContainerPtrToValuePtr<TArray<FDeckWalkSurfaceSettings>>(Area) = {Surface};
			*FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("ObstacleComponentNames"))->ContainerPtrToValuePtr<TArray<FName>>(Area) = {TEXT("TestFloor")};
			UDeckEnemySpawnerComponent* Spawner = Ship->GetDeckEnemySpawnerComponent();
			FindFProperty<FBoolProperty>(Spawner->GetClass(), TEXT("bEnableSpawning"))->SetPropertyValue_InContainer(Spawner, true);
			FindFProperty<FFloatProperty>(Spawner->GetClass(), TEXT("SpawnStartDelay"))->SetPropertyValue_InContainer(Spawner, 0);
			FindFProperty<FFloatProperty>(Spawner->GetClass(), TEXT("SightActivationDelay"))->SetPropertyValue_InContainer(Spawner, 0);
			FindFProperty<FFloatProperty>(Spawner->GetClass(), TEXT("ActivationInterval"))->SetPropertyValue_InContainer(Spawner, .05f);
			TArray<FDeckEnemySpawnSlot>& Plan = *FindFProperty<FArrayProperty>(Spawner->GetClass(), TEXT("SpawnPlan"))->ContainerPtrToValuePtr<TArray<FDeckEnemySpawnSlot>>(Spawner);
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FDeckEnemySpawnSlot& Slot = Plan.AddDefaulted_GetRef();
				Slot.EnemyClass = ADeckEnemy::StaticClass();
				Slot.SpawnPointId = 101 + Index;
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				UChildActorComponent* Child = NewObject<UChildActorComponent>(Ship);
				Ship->AddInstanceComponent(Child);
				Child->SetupAttachment(Deck);
				Child->SetChildActorClass(AChestSpawnPoint::StaticClass());
				Child->RegisterComponent();
				AChestSpawnPoint* Point = CastChecked<AChestSpawnPoint>(Child->GetChildActor());
				Point->ConfigureGuardedSpawn(nullptr, {}, Ship);
				Point->SetEnvironment(EChestEnvironment::ShipDeck);
				Points.Add(Point);
			}
			if (bStartBundleIdle) Ship->SetBundlePoolIdle(true);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			// A minimal fixture has no GameMode to dispatch StartPlay for placed actors.
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FFixture()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			Instance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}

		TArray<AStorageChest*> SpawnChests()
		{
			// Use the manager's production spawn path, including post-spawn guaranteed loot.
			Manager->InitializeDataDrivenChestsWithBalance(nullptr);
			TArray<AStorageChest*> Chests;
			for (AChestSpawnPoint* Point : Points) Chests.Add(Cast<AStorageChest>(Point->GetSpawnedActor()));
			return Chests;
		}
	};

	class FWaitForCrew : public IAutomationLatentCommand
	{
	public:
		FWaitForCrew(TSharedPtr<FFixture> InFixture, FAutomationTestBase* InTest,
			TArray<ADeckEnemy*> InCrew, TFunction<void()> InStart, TFunction<void()> InFinish)
			: Fixture(MoveTemp(InFixture)), Test(InTest), Crew(MoveTemp(InCrew)), Start(MoveTemp(InStart)), Finish(MoveTemp(InFinish)) {}

		virtual bool Update() override
		{
			// Flush initialization/possession readiness callbacks before presenting sight.
			if (Steps == 1) Start();
			// One tick per engine frame lets the production TimerManager advance normally.
			Fixture->World->Tick(LEVELTICK_All, .025f);
			// Minimal worlds can have no ticking level collection. TimerManager guards double ticks.
			Fixture->World->GetTimerManager().Tick(.025f);
			if (Crew[0]->IsPoolActive() && Crew[1]->IsPoolActive())
			{
				Finish();
				return true;
			}
			if (++Steps < 200) return false;
			UDeckEnemySpawnerComponent* Spawner = Fixture->Ship->GetDeckEnemySpawnerComponent();
			Test->AddError(FString::Printf(TEXT("Crew activation timed out after five simulated seconds. WaitReason=%s State=%d Active=%d/%d HostDeployable=%d"),
				*Spawner->GetSpawnWaitReason().ToString(), static_cast<int32>(Spawner->GetDeploymentState()),
				Crew[0]->IsPoolActive(), Crew[1]->IsPoolActive(), Fixture->Ship->CanDeployDeckEnemies()));
			return true;
		}

	private:
		TSharedPtr<FFixture> Fixture;
		FAutomationTestBase* Test;
		TArray<ADeckEnemy*> Crew;
		TFunction<void()> Start;
		TFunction<void()> Finish;
		int32 Steps = 0;
	};

	int32 CountItem(const AStorageChest* Chest, FGameplayTag Tag)
	{
		int32 Count = 0;
		for (const FInventorySlot& Slot : Chest->GetStorageComponent()->GetSlots())
			if (Slot.ItemTag == Tag) Count += Slot.Count;
		return Count;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FShipCrewChestIntegrationTest,
	"ArtisticSW.Integration.ShipCrewChest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FShipCrewChestIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("Crew.ChestBeforeActivation"), TEXT("Crew.ChestAfterActivation"), TEXT("Crew.ProfileReassignment"), TEXT("Crew.PooledDeckPreparation"),
		TEXT("Boss.ChestBeforeSpawn"), TEXT("Boss.ChestAfterSpawn"), TEXT("Boss.FailedSpawn"),
		TEXT("Boss.AuthoredMid1"), TEXT("Boss.AuthoredMid3"), TEXT("Boss.WithLivingCrew"), TEXT("Boss.IdleReturnCleanup")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FShipCrewChestIntegrationTest::RunTest(const FString& Parameters)
{
	// Known unrelated authored recipe errors emitted once when item data is first loaded.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 0);
	const bool bPooledPreparation = Parameters == TEXT("Crew.PooledDeckPreparation");
	TSharedPtr<ShipCrewChestIntegration::FFixture> FixturePtr = MakeShared<ShipCrewChestIntegration::FFixture>(bPooledPreparation);
	ShipCrewChestIntegration::FFixture& Fixture = *FixturePtr;
	if (bPooledPreparation)
	{
		UDeckEnemySpawnerComponent* Spawner = Fixture.Ship->GetDeckEnemySpawnerComponent();
		TestFalse(TEXT("Idle BeginPlay defers deck sampling"), Fixture.Ship->GetDeckWalkAreaComponent()->IsReady());
		TestEqual(TEXT("Idle BeginPlay does not allocate crew before the profile"), Spawner->GetPooledEnemyCount(), 0);
		FString Error;
		if (!TestTrue(TEXT("Restore preparation builds navigation without creating crew"), Fixture.Ship->PrepareBundleDeck(false, Error))) return false;
		TestEqual(TEXT("Restore preparation preserves the empty saved crew state"), Spawner->GetPooledEnemyCount(), 0);
		Fixture.Ship->PrepareFreshBundleEncounter(Fixture.Ship->GetActorTransform());
		if (!TestTrue(TEXT("Fresh preparation succeeds while idle"), Fixture.Ship->PrepareBundleDeck(true, Error))) return false;
		TestTrue(TEXT("Fresh deck is ready"), Fixture.Ship->GetDeckWalkAreaComponent()->IsReady());
		TestEqual(TEXT("Fresh crew plan produces both crew Actors"), Spawner->GetPooledEnemyCount(), 2);
		TestTrue(TEXT("Preparation leaves the host idle"), Fixture.Ship->IsBundlePoolIdle());
		TestFalse(TEXT("Temporary Query collision is restored after sampling"), Fixture.Ship->GetActorEnableCollision());
		TestFalse(TEXT("Preparation does not enable physics"), Fixture.Ship->IsShipRuntimePhysicsEnabled());
		FSWRoomDeckSpawnerState Before, After;
		TArray<FSWRoomCaptureIssue> Issues;
		Spawner->CaptureRoomState(Before, Issues);
		TestTrue(TEXT("Navigation can be rebuilt for restore without resetting the encounter"), Fixture.Ship->PrepareBundleDeck(false, Error));
		Spawner->CaptureRoomState(After, Issues);
		TestTrue(TEXT("Crew identities remain unchanged"), Before.EnemyPoolIds == After.EnemyPoolIds);
		TestEqual(TEXT("Encounter generation remains unchanged"), Before.EncounterGeneration, After.EncounterGeneration);
		TestTrue(TEXT("Deployment slot progress remains unchanged"), Before.SlotResults == After.SlotResults);
		Fixture.Ship->SetBundlePoolIdle(false);
		TestTrue(TEXT("Collision resumes on actual wake"), Fixture.Ship->GetActorEnableCollision());
		return true;
	}
	if (!TestTrue(TEXT("Isolated deck walk surface is ready"), Fixture.Ship->GetDeckWalkAreaComponent()->IsReady())
		|| !TestTrue(TEXT("Ship publishes a deployable runtime state"), Fixture.Ship->CanDeployDeckEnemies())) return false;
	UDeckEnemySpawnerComponent* Spawner = Fixture.Ship->GetDeckEnemySpawnerComponent();
	TArray<ADeckEnemy*> Crew;
	Spawner->GetPooledEnemies(Crew);
	if (!TestEqual(TEXT("Real spawner creates two pool members"), Crew.Num(), 2)) return false;
	for (ADeckEnemy* Enemy : Crew)
	{
		TestEqual(TEXT("Spawned crew recognizes its host ship"), Enemy->GetDeckHostShip(), Fixture.Ship);
		TestTrue(TEXT("Ship recognizes spawned crew ownership"), Fixture.Ship->IsOwnedCannonSplashProtectedActor(Enemy));
		TestFalse(TEXT("Pool creation does not count as actual appearance"), Enemy->IsPoolActive());
	}
	if (Parameters == TEXT("Crew.ProfileReassignment"))
	{
		Fixture.Ship->SetBundlePoolIdle(true);
		Spawner->ReconcileBundleProfile();
		TArray<ADeckEnemy*> Unchanged;
		Spawner->GetPooledEnemies(Unchanged);
		TestTrue(TEXT("Same profile keeps its existing crew bundle"), Unchanged == Crew);
		TArray<FDeckEnemySpawnSlot>& Plan = *FindFProperty<FArrayProperty>(Spawner->GetClass(), TEXT("SpawnPlan"))->ContainerPtrToValuePtr<TArray<FDeckEnemySpawnSlot>>(Spawner);
		Plan.SetNum(1);
		Plan[0].EnemyClass = ADeckRangedEnemy::StaticClass();
		Spawner->ReconcileBundleProfile();
		TestEqual(TEXT("Changed crew profile releases the old pool"), Spawner->GetPooledEnemyCount(), 0);
		for (ADeckEnemy* Enemy : Crew) TestTrue(TEXT("Old crew Actors are destroyed"), Enemy->IsActorBeingDestroyed());
		FString PreparationError;
		if (!TestTrue(TEXT("Changed profile rebuilds the idle deck and crew before wake"), Fixture.Ship->PrepareBundleDeck(true, PreparationError))) return false;
		TArray<ADeckEnemy*> Rebuilt;
		Spawner->GetPooledEnemies(Rebuilt);
		if (TestEqual(TEXT("New profile creates exactly one crew slot"), Rebuilt.Num(), 1))
			TestEqual(TEXT("New crew has the requested class"), Rebuilt[0]->GetClass(), ADeckRangedEnemy::StaticClass());
		FindFProperty<FBoolProperty>(Spawner->GetClass(), TEXT("bEnableSpawning"))->SetPropertyValue_InContainer(Spawner, false);
		Spawner->ReconcileBundleProfile();
		TestEqual(TEXT("Disabled crew profile clears the previous pool"), Spawner->GetPooledEnemyCount(), 0);
		return true;
	}
	const bool bCrewCase = Parameters.StartsWith(TEXT("Crew."));
	const bool bLateChest = Parameters.Contains(TEXT("After"));
	TArray<AStorageChest*> Chests;
	if (!bLateChest)
	{
		Chests = Fixture.SpawnChests();
		for (AStorageChest* Chest : Chests)
		{
			if (!TestNotNull(TEXT("Guarded chest spawns"), Chest)) return false;
			TestFalse(TEXT("Inactive pooled crew leaves chest unlocked"), Chest->IsLocked());
			TestEqual(TEXT("Inactive pooled crew is not a chest guard"), Chest->GetAliveGuardCount(), 0);
		}
	}
	if (bCrewCase)
	{
		ANavalAIController* Controller = Fixture.World->SpawnActor<ANavalAIController>();
		Controller->Possess(Fixture.Ship);
		UAIPerceptionComponent* Perception = Controller->GetPerceptionComponent();
		if (!TestNotNull(TEXT("Naval controller owns perception"), Perception)) return false;
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<ShipCrewChestIntegration::FWaitForCrew>(
			FixturePtr, this, Crew, [this, FixturePtr, Controller, Perception]()
			{
				// Supply a deterministic sight stimulus, then exercise the actual perception handler.
				Perception->RegisterStimulus(FixturePtr->PlayerShip, FAIStimulus(*GetDefault<UAISense_Sight>(), 1,
					FixturePtr->PlayerShip->GetActorLocation(), FixturePtr->Ship->GetActorLocation()));
				Perception->ProcessStimuli();
				TestEqual(TEXT("Naval perception recognizes the player ship"), Controller->FindSightedPlayerShip(), FixturePtr->PlayerShip);
			}, [this, FixturePtr, Crew, Chests, bLateChest]() mutable
		{
			ShipCrewChestIntegration::FFixture& Fixture = *FixturePtr;
			UDeckEnemySpawnerComponent* Spawner = Fixture.Ship->GetDeckEnemySpawnerComponent();
			if (bLateChest) Chests = Fixture.SpawnChests();
			for (AStorageChest* Chest : Chests)
			{
				if (!TestNotNull(TEXT("Chest exists after crew activation"), Chest)) return;
				TestTrue(TEXT("Active crew locks every owned chest"), Chest->IsLocked());
				TestEqual(TEXT("Every chest recognizes both active guards"), Chest->GetAliveGuardCount(), 2);
			}
			Fixture.Ship->RegisterCrewEnemy(Crew[0]);
			Crew[0]->DeactivateToPool();
			for (AStorageChest* Chest : Chests)
			{
				TestEqual(TEXT("Pool deactivation removes the active guard"), Chest->GetAliveGuardCount(), 1);
				TestTrue(TEXT("Remaining guard keeps the chest locked"), Chest->IsLocked());
			}
			ADeckEnemy* Reactivated = nullptr;
			TestTrue(TEXT("Spawner reactivates the returned crew member"), Spawner->ActivateEnemyAtPoint(101, nullptr, Reactivated));
			for (AStorageChest* Chest : Chests) TestEqual(TEXT("Reactivation restores one guard without duplicates"), Chest->GetAliveGuardCount(), 2);
			Crew[0]->GetHealthComponent()->StartDeath();
			for (AStorageChest* Chest : Chests)
			{
				TestEqual(TEXT("Real death notification removes one guard"), Chest->GetAliveGuardCount(), 1);
				TestTrue(TEXT("One survivor keeps lock"), Chest->IsLocked());
			}
			Crew[1]->GetHealthComponent()->StartDeath();
			for (AStorageChest* Chest : Chests)
			{
				TestEqual(TEXT("Final death clears guard count"), Chest->GetAliveGuardCount(), 0);
				TestFalse(TEXT("Final death unlocks every chest"), Chest->IsLocked());
			}
			TestEqual(TEXT("Ship receives crew death notifications"), Fixture.Ship->GetLivingCrewCount(), 0);
			TestTrue(TEXT("Ship records crew defeat"), Fixture.Ship->IsCrewDefeated());
			}));
		return true;
	}

	UStoryFacadeSubsystem* Story = Fixture.Instance->GetSubsystem<UStoryFacadeSubsystem>();
	if (!TestNotNull(TEXT("Campaign state belongs to isolated game instance"), Story)) return false;
	const bool bAuthored = Parameters.Contains(TEXT("Authored"));
	const bool bMid3 = Parameters.Contains(TEXT("Mid3"));
	Story->ActivateDevelopmentMiddleBoss(bMid3 ? 3 : 1);
	UBossEncounterComponent* Encounter = Fixture.Ship->GetBossEncounterComponent();
	if (bMid3)
	{
		FEnumProperty* Required = FindFProperty<FEnumProperty>(Encounter->GetClass(), TEXT("RequiredStoryNode"));
		Required->GetUnderlyingProperty()->SetIntPropertyValue(Required->ContainerPtrToValuePtr<void>(Encounter), static_cast<int64>(EStoryNode::SuppressJapaneseForcesQuestAccepted));
		FEnumProperty* Stop = FindFProperty<FEnumProperty>(Encounter->GetClass(), TEXT("StopAfterStoryNode"));
		Stop->GetUnderlyingProperty()->SetIntPropertyValue(Stop->ContainerPtrToValuePtr<void>(Encounter), static_cast<int64>(EStoryNode::MiddleBoss3Defeated));
	}
	FEnumProperty* Trigger = FindFProperty<FEnumProperty>(Encounter->GetClass(), TEXT("EncounterTrigger"));
	Trigger->GetUnderlyingProperty()->SetIntPropertyValue(Trigger->ContainerPtrToValuePtr<void>(Encounter), static_cast<int64>(EBossEncounterTrigger::PlayerShipSight));
	const bool bFailedSpawn = Parameters.Contains(TEXT("Failed"));
	UClass* BossClass = AShipBossEnemy::StaticClass();
	if (bAuthored)
	{
		BossClass = LoadClass<AShipBossEnemy>(nullptr, bMid3
			? TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T4/T4_BP_ShipBoss_Samurai.T4_BP_ShipBoss_Samurai_C")
			: TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T2/T2_BP_ShipBoss_Rogue.T2_BP_ShipBoss_Rogue_C"));
		if (!TestNotNull(TEXT("Actual Lvl_CY boss Blueprint loads"), BossClass)) return false;
	}
	Encounter->ConfigureEncounter(nullptr, BossClass, bFailedSpawn ? 999 : 103);
	if (bFailedSpawn)
	{
		AddExpectedError(TEXT("Reason=UnknownSpawnAnchor"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Invalid boss point fails spawn"), Encounter->NotifyPlayerShipSighted(Fixture.PlayerShip));
		TestEqual(TEXT("Failed spawn is terminal"), Encounter->GetEncounterState(), EBossEncounterState::Failed);
		TestNull(TEXT("Failed spawn registers no boss"), Fixture.Ship->GetRegisteredBossEnemy());
		for (AStorageChest* Chest : Chests)
		{
			TestFalse(TEXT("Failed boss does not lock a guardless chest"), Chest->IsLocked());
			TestFalse(TEXT("Failed boss does not classify chest as boss reward"), Chest->IsBossChest());
		}
		return true;
	}
	UBossChestGuaranteedLootData* Loot = bAuthored
		? LoadObject<UBossChestGuaranteedLootData>(nullptr, TEXT("/Game/Blueprints/Item/Data/DA_BossChestGuaranteedLoot.DA_BossChestGuaranteedLoot"))
		: NewObject<UBossChestGuaranteedLootData>(Fixture.Manager);
	if (!TestNotNull(TEXT("Guaranteed loot data exists"), Loot)) return false;
	if (!bAuthored)
	{
		FGuaranteedBossLootEntry& Entry = Loot->Entries.AddDefaulted_GetRef();
		Entry.BossClass = BossClass;
		FStorageItemEntry& Item = Entry.GuaranteedItems.AddDefaulted_GetRef();
		Item.ItemTag = Item_Id_Material_ShipMaterials_WoodenPlank;
		Item.Count = 2;
	}
	TArray<FStorageItemEntry> RewardItems;
	if (!TestTrue(TEXT("Exact summoned class maps to required reward"), Loot->FindItemsForExactClass(BossClass, RewardItems))
		|| !TestTrue(TEXT("Required reward is nonempty"), !RewardItems.IsEmpty())) return false;
	FSoftObjectProperty* LootProperty = FindFProperty<FSoftObjectProperty>(Fixture.Manager->GetClass(), TEXT("BossGuaranteedLootData"));
	LootProperty->SetObjectPropertyValue_InContainer(Fixture.Manager, Loot);
	// Native test boss intentionally has no authored combat tree; combat AI is outside this fixture.
	const bool bIdleCleanup = Parameters == TEXT("Boss.IdleReturnCleanup");
	if (!bAuthored) AddExpectedError(TEXT("Boss has no Behavior Tree. BT-only AI will remain stopped."), EAutomationExpectedErrorFlags::Contains, bIdleCleanup ? 2 : 1);
	const bool bMixedGuards = Parameters.Contains(TEXT("WithLivingCrew")) || bIdleCleanup;
	if (bMixedGuards)
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			ADeckEnemy* Activated = nullptr;
			TestTrue(TEXT("Real spawner activates a regular guard before boss spawn"), Spawner->ActivateEnemyAtPoint(101 + Index, nullptr, Activated));
		}
	}
	TestTrue(TEXT("Actual boss encounter spawn succeeds"), Encounter->NotifyPlayerShipSighted(Fixture.PlayerShip));
	AShipBossEnemy* Boss = Encounter->GetSpawnedBoss();
	if (!TestNotNull(TEXT("Encounter creates a boss"), Boss)) return false;
	TestEqual(TEXT("Boss recognizes host ship"), Boss->GetHostShip(), Fixture.Ship);
	TestEqual(TEXT("Ship recognizes summoned boss"), Fixture.Ship->GetRegisteredBossEnemy(), Boss);
	TestEqual(TEXT("Boss does not inflate ordinary crew count"), Fixture.Ship->GetLivingCrewCount(), 2);
	if (bLateChest) Chests = Fixture.SpawnChests();
	for (int32 Index = 0; Index < Chests.Num(); ++Index)
	{
		AStorageChest* Chest = Chests[Index];
		if (!TestNotNull(TEXT("Boss guarded chest exists"), Chest)) return false;
		TestEqual(TEXT("Spawn point recognizes exact boss"), Fixture.Points[Index]->GetRegisteredBossGuard(), static_cast<ABaseCharacter*>(Boss));
		TestTrue(TEXT("Actual summoned boss locks chest"), Chest->IsLocked());
		TestTrue(TEXT("Actual summoned boss classifies reward"), Chest->IsBossChest());
		for (const FStorageItemEntry& Item : RewardItems)
			TestEqual(TEXT("Boss spawn injects guaranteed item immediately, including late chest"), ShipCrewChestIntegration::CountItem(Chest, Item.ItemTag), Item.Count);
		Fixture.Manager->EnsureBossGuaranteedLoot(Fixture.Points[Index]);
		Fixture.Manager->EnsureBossGuaranteedLoot(Fixture.Points[Index]);
		for (const FStorageItemEntry& Item : RewardItems)
			TestEqual(TEXT("Repeated reward application is idempotent"), ShipCrewChestIntegration::CountItem(Chest, Item.ItemTag), Item.Count);
	}
	if (bIdleCleanup)
	{
		AController* BossController = Boss->GetController();
		TArray<AController*> CrewControllers;
		for (ADeckEnemy* Enemy : Crew) if (Enemy->GetController()) CrewControllers.Add(Enemy->GetController());
		const bool bCollisionBefore = Fixture.Ship->GetActorEnableCollision();
		const bool bPhysicsBefore = Fixture.Ship->IsShipRuntimePhysicsEnabled();
		TestTrue(TEXT("Living deployed crew no longer vetoes distance suspension"), Spawner->CanSuspendForDistanceOptimization());
		Fixture.Ship->GetNavigationComponent()->OnNavigationStateChanged.Broadcast(ENavalCombatState::Return, ENavalCombatState::Idle);
		TestEqual(TEXT("Idle deletes the ordinary crew pool"), Spawner->GetPooledEnemyCount(), 0);
		for (ADeckEnemy* Enemy : Crew) TestTrue(TEXT("Ordinary crew Actor is destroyed"), Enemy->IsActorBeingDestroyed());
		for (AController* Controller : CrewControllers) TestTrue(TEXT("Ordinary crew Controller is destroyed"), Controller->IsActorBeingDestroyed());
		TestTrue(TEXT("Idle also destroys the boss"), Boss->IsActorBeingDestroyed());
		if (BossController) TestTrue(TEXT("Idle also destroys the boss Controller"), BossController->IsActorBeingDestroyed());
		TestNull(TEXT("Boss reference cleared"), Fixture.Ship->GetRegisteredBossEnemy());
		TestEqual(TEXT("Boss returns to its rule-based waiting state"), Encounter->GetEncounterState(), EBossEncounterState::Waiting);
		TestFalse(TEXT("Administrative despawn does not count as crew defeat"), Fixture.Ship->IsCrewDefeated());
		TestFalse(TEXT("Near Idle hull is still an assigned Actor"), Fixture.Ship->IsBundlePoolIdle());
		TestEqual(TEXT("Idle cleanup keeps hull collision"), Fixture.Ship->GetActorEnableCollision(), bCollisionBefore);
		TestEqual(TEXT("Idle cleanup keeps hull buoyancy/physics"), Fixture.Ship->IsShipRuntimePhysicsEnabled(), bPhysicsBefore);
		Fixture.Ship->bEnableDistanceOptimization = true;
		Fixture.Ship->GetNavigationComponent()->ResetSpawnHome(Fixture.Ship->GetActorTransform());
		TestTrue(TEXT("Home Idle hull remains eligible for distant suspension"), Fixture.Ship->CanEnterDistanceOptimizationDormancy());
		TestTrue(TEXT("Existing boss rules can spawn again on renewed sight"), Encounter->NotifyPlayerShipSighted(Fixture.PlayerShip));
		TestNotNull(TEXT("A replacement boss exists"), Encounter->GetSpawnedBoss());
		const float Range = Fixture.Ship->GetDistanceOptimizationRange();
		Fixture.PlayerShip->SetActorLocation(Fixture.Ship->GetActorLocation() + FVector(Range * 0.25f, 0, 0));
		UShipSwarmSubsystem* Swarm = Fixture.World->GetSubsystem<UShipSwarmSubsystem>();
		Swarm->EvaluateDistanceOptimization();
		TestFalse(TEXT("Player nearby keeps an Idle hull awake"), Fixture.Ship->IsDistanceOptimizationDormant());
		// A closed story gate suppresses visibility/encounters, not distant Actor retirement.
		Fixture.Ship->SquadID = TEXT("Final");
		TestTrue(TEXT("Closed Final gate is represented in the fixture"), Fixture.Ship->IsStoryGateDormant());
		Fixture.PlayerShip->SetActorLocation(Fixture.Ship->GetActorLocation() + FVector(Range * 2.0f + 1000, 0, 0));
		Swarm->EvaluateDistanceOptimization();
		TestTrue(TEXT("Player far away suspends the Home Idle hull"), Fixture.Ship->IsDistanceOptimizationDormant());
		TestNull(TEXT("Distant Idle suspension leaves no boss behind"), Fixture.Ship->GetRegisteredBossEnemy());
		return true;
	}
	Boss->GetHealthComponent()->StartDeath();
	TestEqual(TEXT("Encounter receives actual boss death"), Encounter->GetEncounterState(), EBossEncounterState::Defeated);
	for (AStorageChest* Chest : Chests)
	{
		TestEqual(TEXT("Boss death lock depends on surviving regular guards"), Chest->IsLocked(), bMixedGuards);
		for (const FStorageItemEntry& Item : RewardItems)
			TestEqual(TEXT("Boss death preserves guaranteed reward"), ShipCrewChestIntegration::CountItem(Chest, Item.ItemTag), Item.Count);
	}
	if (bMixedGuards)
	{
		Crew[0]->GetHealthComponent()->StartDeath();
		for (AStorageChest* Chest : Chests) TestTrue(TEXT("Boss and one regular guard dead still leaves chest locked"), Chest->IsLocked());
		Crew[1]->GetHealthComponent()->StartDeath();
		for (AStorageChest* Chest : Chests)
		{
			TestFalse(TEXT("Combined boss and crew deaths unlock only after final guard"), Chest->IsLocked());
			for (const FStorageItemEntry& Item : RewardItems)
				TestEqual(TEXT("Combined guard deaths preserve required loot"), ShipCrewChestIntegration::CountItem(Chest, Item.ItemTag), Item.Count);
		}
	}
	TestFalse(TEXT("Repeated sight does not spawn another boss"), Encounter->NotifyPlayerShipSighted(Fixture.PlayerShip));
	return true;
}

#endif
