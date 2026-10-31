#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AI/BaseAIController.h"
#include "BasePlayer.h"
#include "BossAI/GunnerBossEnemy.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Task/BTT_SummonDeckEnemy.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Components/BaseHealthComponent.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGunnerPeriodicSummonTest,
    "ArtisticSW.Enemy.GunnerPeriodicSummon.BusyCapReuseAndCrew",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGunnerPeriodicSummonTest::RunTest(const FString& Parameters)
{
    FEditorScriptExecutionGuard ScriptExecutionGuard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GunnerSummonWorld"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->CreateAISystem();
    ON_SCOPE_EXIT { FlushAsyncLoading(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    // This isolated world deliberately does not BeginPlay ships. Register native
    // attribute subobjects explicitly, including deferred reserve actors.
    const auto SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([](AActor* Actor)
    {
        if (auto* Enemy = Cast<ABaseEnemy>(Actor))
        {
            auto* ASC = Enemy->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Enemy, Enemy);
            ASC->AddAttributeSetSubobject(CastChecked<UBaseAttributeSet>(Enemy->GetDefaultSubobjectByName(TEXT("BasicAttributeSet"))));
        }
    }));
    ON_SCOPE_EXIT { World->RemoveOnActorSpawnedHandler(SpawnHandle); };
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Ship = World->SpawnActor<AEnemyShip>(Spawn);
    Ship->BuoyancyRoot->SetSimulatePhysics(false);
    auto* Frame = Ship->GetShipDeckMesh();
    Frame->SetRelativeScale3D(FVector::OneVector);
    Frame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Floor = NewObject<UStaticMeshComponent>(Ship, TEXT("SummonTestFloor"));
    Ship->AddInstanceComponent(Floor);
    Floor->SetupAttachment(Frame);
    Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetRelativeScale3D(FVector(20.f, 20.f, 0.1f));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Floor->SetCollisionResponseToAllChannels(ECR_Block);
    Floor->RegisterComponent();
    auto* Area = Ship->GetDeckWalkAreaComponent();
    FDeckWalkSurfaceSettings Surface;
    Surface.SurfaceId = TEXT("SummonDeck"); Surface.FloorComponentNames = {TEXT("SummonTestFloor")};
    Surface.bTraceComplex = false; Surface.MinimumFloorZ = 0.f; Surface.MaximumFloorZ = 20.f;
    *FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("Surfaces"))->ContainerPtrToValuePtr<TArray<FDeckWalkSurfaceSettings>>(Area) = {Surface};
    *FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("ObstacleComponentNames"))->ContainerPtrToValuePtr<TArray<FName>>(Area) = {TEXT("SummonTestFloor")};
    Frame->UpdateComponentToWorld(); Floor->RecreatePhysicsState(); Area->Rebuild();
    if (!TestTrue(TEXT("Test deck built"), Area->IsReady())) return false;
    for (int32 Id : {10, 11})
    {
        auto* Point = NewObject<UDeckWaypointComponent>(Ship);
        Ship->AddInstanceComponent(Point); Point->SetupAttachment(Frame);
        Point->SetWaypointIdForAuthoring(Id);
        Point->SetRelativeLocation(Id == 10 ? FVector(400.f, 250.f, 10.f) : FVector(700.f, -250.f, 10.f));
        FindFProperty<FBoolProperty>(Point->GetClass(), TEXT("bCanSpawn"))->SetPropertyValue_InContainer(Point, true);
        FindFProperty<FNameProperty>(Point->GetClass(), TEXT("WalkSurfaceId"))->SetPropertyValue_InContainer(Point, Surface.SurfaceId);
        Point->RegisterComponent();
    }
    auto* Pool = Ship->GetDeckEnemySpawnerComponent();
    Pool->InitializeWaypoints();
    auto* Runtime = FindFProperty<FStructProperty>(Ship->GetClass(), TEXT("RuntimeState"))->ContainerPtrToValuePtr<FEnemyShipRuntimeState>(Ship);
    Runtime->Phase = EEnemyShipRuntimePhase::Active; Runtime->Revision = 1;
    if (!TestTrue(TEXT("Host is ready to deploy"), Ship->CanDeployDeckEnemies())) return false;
    auto* Boss = World->SpawnActor<AGunnerBossEnemy>(FVector(-700.f, 0.f, 100.f), FRotator::ZeroRotator, Spawn);
    FindFProperty<FObjectPropertyBase>(Boss->GetClass(), TEXT("HostShip"))->SetObjectPropertyValue_InContainer(Boss, Ship);
    Boss->AdditionalSummonPoolCapacity = 2;
    Boss->SummonedEnemyClass = LoadClass<ADeckEnemy>(nullptr,
        TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_SummonedRanged.BP_T1Boss_SummonedRanged_C"));
    if (!TestNotNull(TEXT("Dedicated ranged summon BP exists"), Boss->SummonedEnemyClass.Get())) return false;
    FindFProperty<FFloatProperty>(Boss->GetClass(), TEXT("SummonCooldown"))->SetPropertyValue_InContainer(Boss, 0.f);
    TestTrue(TEXT("Register summoner as living encounter member"), Ship->RegisterBossEnemy(Boss));
    TestEqual(TEXT("Boss alone keeps the encounter active"), Ship->GetLivingCrewCount(), 1);
    auto* Player = World->SpawnActor<ABasePlayer>(FVector(-300.f, 0.f, 100.f), FRotator::ZeroRotator, Spawn);
    Boss->SetBossCombatTarget(Player);
    auto* AI = World->SpawnActor<ABaseAIController>(Spawn); AI->SetPawn(Boss);
    auto* Tree = NewObject<UBehaviorTreeComponent>(AI); Tree->RegisterComponent();
    auto* Task = NewObject<UBTT_SummonDeckEnemy>(); Task->bWaitForBossIdle = true;
    Boss->GetAbilitySystemComponent()->AddLooseGameplayTag(State_Boss_Busy);
    TestEqual(TEXT("Summon waits while a shot owns the boss"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::InProgress);
    TestEqual(TEXT("Waiting creates no enemy"), Pool->GetPooledEnemyCount(), 0);
    Boss->GetAbilitySystemComponent()->RemoveLooseGameplayTag(State_Boss_Busy);
    if (!TestEqual(TEXT("Idle task summons into a host without a ranged spawn plan"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::Succeeded)) return false;
    TestEqual(TEXT("Second interval can fill the second slot"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::Succeeded);
    TestEqual(TEXT("Live summon cap skips further intervals"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::Failed);
    TestEqual(TEXT("Reserve remains bounded"), Pool->GetPooledEnemyCount(), 2);
    TArray<ADeckEnemy*> Enemies; Pool->GetPooledEnemies(Enemies);
    if (Enemies.Num() != 2) return false;
    TestEqual(TEXT("Boss and two live summons count toward the encounter"), Ship->GetLivingCrewCount(), 3);
    Enemies[0]->DeactivateToPool();
    TestEqual(TEXT("Inactive reserve does not block capture"), Ship->GetLivingCrewCount(), 2);
    TestEqual(TEXT("A later interval reuses the inactive summon"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::Succeeded);
    TestEqual(TEXT("Reuse does not grow the pool"), Pool->GetPooledEnemyCount(), 2);
    Enemies[1]->DeactivateToPool();
    TestTrue(TEXT("A reused actor counts once toward the live cap"), Boss->CanSummonDeckEnemy());
    TestEqual(TEXT("The freed second slot remains usable"), Task->ExecuteTask(*Tree, nullptr), EBTNodeResult::Succeeded);
    for (auto* Enemy : Enemies)
        Enemy->FindComponentByClass<USWRoomSnapshotComponent>()->SetRuntimeId(FGuid::NewGuid());
    FSWRoomDeckSpawnerState SavedPool;
    TArray<FSWRoomCaptureIssue> Issues;
    Pool->CaptureRoomState(SavedPool, Issues);
    TestEqual(TEXT("Room save preserves both summon reserve identities"), SavedPool.SummonReservePoolIds.Num(), 2);
    FString Error;
    TestTrue(TEXT("Extra summons do not invalidate the regular spawn plan"), Pool->AuditBundleCrew(true, Error));
    for (auto* Enemy : Enemies) Enemy->DeactivateToPool();
    TestEqual(TEXT("Released summons leave no deployed ledger entries"), Pool->GetAliveDeployedEnemyCount(), 0);
    TestEqual(TEXT("Only the boss remains after summon cleanup"), Ship->GetLivingCrewCount(), 1);
    TestFalse(TEXT("Original guard loss cannot prematurely end the encounter"), Ship->IsCrewDefeated());
    TestTrue(TEXT("Boss can continue summoning after original crew are gone"), Boss->CanSummonDeckEnemy());
    Boss->GetHealthComponent()->InitializeWithAbilitySystem(Boss->GetAbilitySystemComponent(), false);
    Boss->GetHealthComponent()->StartDeath();
    Boss->OnBaseEnemyDeathNotified.Broadcast(Boss, EWaveEnemyRemoveReason::Death);
    TestEqual(TEXT("Unused reserve cannot count as living crew after boss death"), Ship->GetLivingCrewCount(), 0);
    TestTrue(TEXT("Boss death completes the encounter"), Ship->IsCrewDefeated());
    return !HasAnyErrors();
}
#endif
