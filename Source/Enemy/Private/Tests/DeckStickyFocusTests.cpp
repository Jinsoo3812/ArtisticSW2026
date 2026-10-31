#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AI/BaseAIController.h"
#include "BasePlayer.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/BossGunAimLine.h"
#include "Materials/MaterialInterface.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Perception/AIPerceptionTypes.h"
#include "ShipAI/EnemyShip.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeckStickyFocusTest, "ArtisticSW.Enemy.ShotFocus.SightLossAndTaskRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeckStickyFocusTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DeckStickyFocusWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->CreateAISystem();
	ON_SCOPE_EXIT { FlushAsyncLoading(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyShip* Ship = World->SpawnActor<AEnemyShip>(Spawn);
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	auto* Frame = Ship->GetShipDeckMesh();
	Frame->SetRelativeScale3D(FVector::OneVector);
	Frame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	auto* Floor = NewObject<UStaticMeshComponent>(Ship, TEXT("FocusTestFloor"));
	Ship->AddInstanceComponent(Floor);
	Floor->SetupAttachment(Frame);
	Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetRelativeScale3D(FVector(20.f, 20.f, 0.1f));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->RegisterComponent();
	auto* Area = Ship->GetDeckWalkAreaComponent();
	FDeckWalkSurfaceSettings Surface;
	Surface.SurfaceId = TEXT("FocusDeck"); Surface.FloorComponentNames = {TEXT("FocusTestFloor")};
	Surface.bTraceComplex = false; Surface.MinimumFloorZ = 0.f; Surface.MaximumFloorZ = 20.f;
	*FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("Surfaces"))->ContainerPtrToValuePtr<TArray<FDeckWalkSurfaceSettings>>(Area) = {Surface};
	*FindFProperty<FArrayProperty>(Area->GetClass(), TEXT("ObstacleComponentNames"))->ContainerPtrToValuePtr<TArray<FName>>(Area) = {TEXT("FocusTestFloor")};
	Frame->UpdateComponentToWorld();
	Floor->RecreatePhysicsState();
	Area->Rebuild();
	if (!TestTrue(TEXT("Test deck built"), Area->IsReady())) return false;
	// Self-contained keys isolate focus policy from unrelated behavior-tree authoring.
	auto* Blackboard = NewObject<UBlackboardData>(World);
	FBlackboardEntry StateEntry;
	StateEntry.EntryName = TEXT("State");
	auto* EnumKey = NewObject<UBlackboardKeyType_Enum>(Blackboard);
	EnumKey->EnumType = StaticEnum<EEnemyAIState>();
	StateEntry.KeyType = EnumKey;
	Blackboard->Keys.Add(StateEntry);
	FBlackboardEntry TargetEntry;
	TargetEntry.EntryName = TEXT("TargetActor");
	TargetEntry.KeyType = NewObject<UBlackboardKeyType_Object>(Blackboard);
	Blackboard->Keys.Add(TargetEntry);
	for (bool bBoss : {false, true})
	{
		ABaseEnemy* Enemy = bBoss ? static_cast<ABaseEnemy*>(World->SpawnActor<AGunnerBossEnemy>(Spawn))
			: static_cast<ABaseEnemy*>(World->SpawnActor<ADeckEnemy>(Spawn));
		if (auto* Deck = Cast<ADeckEnemy>(Enemy)) Deck->SetHostShip(Ship);
		else FindFProperty<FObjectPropertyBase>(Enemy->GetClass(), TEXT("HostShip"))->SetObjectPropertyValue_InContainer(Enemy, Ship);
		auto* AI = World->SpawnActor<ABaseAIController>(Spawn);
		AI->SetPawn(Enemy);
		UBlackboardComponent* BB = nullptr;
		if (!TestTrue(TEXT("AI system initializes the test Blackboard"), AI->UseBlackboard(Blackboard, BB))) return false;
		auto* Player = World->SpawnActor<ABasePlayer>(FVector(100.f, 100.f, 100.f), FRotator::ZeroRotator, Spawn);
		TestTrue(TEXT("Acquire valid player"), AI->SetCombatTarget(Player));
		TestEqual(TEXT("Target acquisition enters Combat"), AI->GetEnemyState(), EEnemyAIState::Combat);
		TestTrue(TEXT("Acquired player has a valid same-deck tracking anchor"), AI->ShouldRetainDeckCombatTarget());
		FAIStimulus LostSight;
		AI->HandleSightStimulus(Player, LostSight);
		TestEqual(TEXT("Brief sight loss keeps player on the deck"), AI->GetCombatTarget(), static_cast<AActor*>(Player));
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		TestTrue(TEXT("Next task can refresh player focus"), AI->RefreshDeckCombatFocus());
		TestEqual(TEXT("Focus restored after another task cleared it"), AI->GetFocusActor(), static_cast<AActor*>(Player));
		Player->SetActorLocation(FVector(20000.f, 0.f, 100.f));
		Enemy->FindComponentByClass<UDeckCombatTargetResolverComponent>()->Reset();
		AI->Tick(0.3f);
		TestNull(TEXT("Player leaving deck expires sticky target"), AI->GetCombatTarget());
		Player->Destroy(); AI->SetPawn(nullptr); AI->Destroy(); Enemy->Destroy();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGunnerShotCylinderTest, "ArtisticSW.Enemy.ShotFocus.ShotCylinder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGunnerShotCylinderTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GunnerCylinderWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Boss = World->SpawnActor<AGunnerBossEnemy>();
	Boss->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Fab/Stylized_Warrior/SKM_Warrior3.SKM_Warrior3")));
	if (!TestTrue(TEXT("Muzzle endpoint can resolve a skeletal socket or bone"), Boss->GetMesh()->DoesSocketExist(TEXT("root")))) return false;
	auto* Target = World->SpawnActor<AActor>();
	auto* TargetRoot = NewObject<USceneComponent>(Target);
	Target->SetRootComponent(TargetRoot);
	TargetRoot->RegisterComponent();
	const FVector Start = Boss->GetMesh()->GetSocketLocation(TEXT("root"));
	Target->SetActorLocation(Start + FVector(600.f, 0.f, 0.f));
	const FVector End = Target->GetActorLocation() + FVector(0.f, 25.f, 10.f);
	auto* Line = World->SpawnActor<ABossGunAimLine>();
	FGunnerBossRaySettings Settings;
	Settings.ShotRadius = 9.f;
	Settings.ShotMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T1/VFX/M_T1Boss_ShotCylinder.M_T1Boss_ShotCylinder"));
	if (!TestNotNull(TEXT("Authored shot material exists"), Settings.ShotMaterial.Get())) return false;
	Line->Initialize(Boss, TEXT("root"), Settings);
	auto* Mesh = Line->FindComponentByClass<UStaticMeshComponent>();
	Line->SetAim(EBossGunAimPhase::Tracking, End, 0.f, 2.f, TargetRoot);
	Line->Tick(0.f);
	TestFalse(TEXT("Early preparation keeps the beam hidden"), Mesh->IsVisible());
	TestFalse(TEXT("Beam stays hidden more than 0.5 seconds before the shot"), Line->IsVisibleAt(1.499f));
	TestTrue(TEXT("Beam starts exactly 0.5 seconds before the shot"), Line->IsVisibleAt(1.5f));
	Line->SetAim(EBossGunAimPhase::Locked, End, 0.f, 2.f, TargetRoot);
	Line->Tick(0.f);
	TestFalse(TEXT("Aim lock does not override the visibility deadline"), Mesh->IsVisible());
	TestTrue(TEXT("Final locked preparation is visible"), Line->IsVisibleAt(1.999f));
	Line->ShowShot(Start, End, TargetRoot);
	TestTrue(TEXT("Confirmed firing shows the live beam"), Mesh->IsVisible());
	TestTrue(TEXT("Beam remains while the attack is active"), Line->IsVisibleAt(0.999f));
	Line->SetAim(EBossGunAimPhase::Tracking, End, 0.2f, 0.85f, TargetRoot, NAME_None, 1.2f);
	TestTrue(TEXT("A burst keeps its beam between shots"), Line->IsVisibleAt(1.1f));
	Line->SetAim(EBossGunAimPhase::Tracking, End, 0.2f, 2.4f, TargetRoot, NAME_None, 3.f);
	TestTrue(TEXT("An active attack does not expire the beam after one second"), Line->IsVisibleAt(1.1f));
	// Recovery belongs to the attack and keeps the live beam until completion.
	Line->SetAim(EBossGunAimPhase::Recovery, FVector::ZeroVector, 0.f, 0.35f);
	TestTrue(TEXT("Long recovery keeps the beam beyond the old lifetime"), Line->IsVisibleAt(2.f));
	TestTrue(TEXT("User radius is applied in world units"), FMath::IsNearlyEqual(Line->GetActorScale3D().X * 50.f, 9.f, 0.01f));
	TestTrue(TEXT("Cylinder spans the live endpoints"), FMath::IsNearlyEqual(Line->GetActorScale3D().Z * 100.f, (End - Start).Size(), 0.01));
	TestEqual(TEXT("Cylinder never becomes a damage or cover collider"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	Boss->AddActorWorldOffset(FVector(140.f, 70.f, 20.f));
	Target->AddActorWorldOffset(FVector(100.f, -20.f, 40.f));
	Target->SetActorRotation(FRotator(0.f, 70.f, 0.f));
	Line->Tick(0.f);
	FVector LiveStart, LiveEnd;
	TestTrue(TEXT("Moved endpoints still resolve"), Line->ResolveEndpoints(LiveStart, LiveEnd));
	TestTrue(TEXT("Start follows the current muzzle rather than the historical shot"), LiveStart.Equals(Boss->GetMesh()->GetSocketLocation(TEXT("root")), 0.01));
	TestFalse(TEXT("Muzzle actually moved in this test"), LiveStart.Equals(Start, 0.01));
	TestTrue(TEXT("End follows target-local impact through translation and rotation"),
		LiveEnd.Equals(TargetRoot->GetComponentTransform().TransformPosition(FVector(0.f, 25.f, 10.f)), 0.01));
	TestTrue(TEXT("Cylinder midpoint follows both endpoints"), Line->GetActorLocation().Equals((LiveStart + LiveEnd) * 0.5f, 0.01));
	Line->SetAim(EBossGunAimPhase::Recovery, FVector::ZeroVector, 0.f, 2.f);
	World->Tick(LEVELTICK_All, 0.3f);
	Line->Tick(0.3f);
	TestTrue(TEXT("Beam is still visible immediately before attack completion"), Mesh->IsVisible());
	Line->FinishPresentation(false);
	TestTrue(TEXT("Attack end destroys the presentation immediately"), Line->IsActorBeingDestroyed());
	TestFalse(TEXT("Attack end leaves no visible beam"), Mesh->IsVisible());
	TestFalse(TEXT("Normal completion before one second has no remaining visual tail"), Line->IsVisibleAt(0.3f));
	TestTrue(TEXT("Normal completion does not schedule a delayed lifespan"), Line->GetLifeSpan() == 0.f);
	auto* CancelledLine = World->SpawnActor<ABossGunAimLine>();
	CancelledLine->Initialize(Boss, TEXT("root"), Settings);
	CancelledLine->ShowShot(Start, End);
	auto* CancelledMesh = CancelledLine->FindComponentByClass<UStaticMeshComponent>();
	TestTrue(TEXT("Another committed shot starts visible"), CancelledMesh->IsVisible());
	CancelledLine->FinishPresentation(true);
	TestTrue(TEXT("Cancellation also destroys the presentation immediately"), CancelledLine->IsActorBeingDestroyed());
	TestFalse(TEXT("Cancellation leaves no visible beam"), CancelledMesh->IsVisible());
	auto* InvalidLine = World->SpawnActor<ABossGunAimLine>();
	InvalidLine->Initialize(Boss, TEXT("root"), Settings);
	InvalidLine->ShowShot(Start, End, TargetRoot);
	TargetRoot->DestroyComponent();
	InvalidLine->Tick(0.f);
	TestFalse(TEXT("A destroyed endpoint cannot leave a floating stale beam"),
		InvalidLine->FindComponentByClass<UStaticMeshComponent>()->IsVisible());
	InvalidLine->FinishPresentation();
	return !HasAnyErrors();
}
#endif
