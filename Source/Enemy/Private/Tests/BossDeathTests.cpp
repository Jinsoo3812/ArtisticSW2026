#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

namespace BossDeathTests
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BossDeathTestWorld"));
		FWorldScope()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FWorldScope() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	};

	AEnemyShip* CreateShip(UWorld* World)
	{
		AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
		Ship->BuoyancyRoot->SetSimulatePhysics(false);
		UDeckWaypointComponent* Point = NewObject<UDeckWaypointComponent>(Ship);
		Ship->AddInstanceComponent(Point);
		Point->SetupAttachment(Ship->GetShipDeckMesh());
		Point->InitializeGeneratedWaypoint(701, 0, 0, true, true, true);
		Point->RegisterComponent();
		Ship->GetDeckEnemySpawnerComponent()->InitializeWaypoints();
		return Ship;
	}

	AShipBossEnemy* CreateBoss(UWorld* World, UClass* BossClass = AShipBossEnemy::StaticClass())
	{
		AShipBossEnemy* Boss = World->SpawnActorDeferred<AShipBossEnemy>(BossClass, FTransform::Identity);
		// The death pipeline is under test, not BT startup in a world without an
		// AI system/game mode. Keep the production Blueprint's montage and mesh.
		Boss->AutoPossessAI = EAutoPossessAI::Disabled;
		Boss->FinishSpawning(FTransform::Identity);
		Boss->DispatchBeginPlay();
		return Boss;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossDeathDeckAnchorTest,
	"ArtisticSW.Enemy.BossDeath.DeckAnchorAndWeaponRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossDeathDeckAnchorTest::RunTest(const FString& Parameters)
{
	// Known unrelated item-table validation emitted by the world subsystem.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	BossDeathTests::FWorldScope Scope;
	AEnemyShip* Ship = BossDeathTests::CreateShip(Scope.World);
	AShipBossEnemy* Boss = BossDeathTests::CreateBoss(Scope.World);
	if (!TestTrue(TEXT("Boss initializes at an occupied deck point"), Boss->InitializeBoss(Ship, 701, nullptr)))
	{
		return false;
	}
	ABaseWeapon* Weapon = Scope.World->SpawnActor<ABaseWeapon>();
	Weapon->SetOwner(Boss);
	Weapon->AttachToComponent(Boss->GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
	FObjectProperty* WeaponProperty = FindFProperty<FObjectProperty>(UBaseWeaponComponent::StaticClass(), TEXT("CurrentWeapon"));
	WeaponProperty->SetObjectPropertyValue_InContainer(Boss->GetWeaponComponent(), Weapon);

	// Deliberately off the occupied waypoint, as during a dash. Hidden relocation
	// cleanup must reveal this exact pose without re-enabling walking/collision.
	const FTransform DeathWorld(FRotator(4.0, 31.0, -3.0), FVector(137.0, -83.0, 220.0));
	Boss->SetActorTransform(DeathWorld);
	const FTransform DeathLocal = DeathWorld.GetRelativeTransform(Ship->GetShipDeckMesh()->GetComponentTransform());
	TestTrue(TEXT("Hidden relocation can start before death"), Boss->BeginHiddenRelocation());
	Boss->GetHealthComponent()->StartDeath();
	TestEqual(TEXT("Missing death GA finishes safely"), Boss->GetHealthComponent()->GetDeathState(), EBaseDeathState::DeathFinished);
	TestTrue(TEXT("Death never snaps to a waypoint"), Boss->GetActorTransform().Equals(DeathWorld, 0.01f));
	TestTrue(TEXT("Attachment preserves the death local transform"), Boss->GetRootComponent()->GetRelativeTransform().Equals(DeathLocal, 0.01f));
	TestTrue(TEXT("Corpse is attached to the moving deck"), Boss->GetRootComponent()->GetAttachParent() == Ship->GetShipDeckMesh());
	TestTrue(TEXT("Weapon is destroyed immediately"), Weapon->IsActorBeingDestroyed());
	TestNull(TEXT("Loadout reference is cleared"), Boss->GetWeaponComponent()->GetCurrentWeapon());
	TestFalse(TEXT("Vanish cannot hide the corpse"), Boss->IsHidden());
	TestFalse(TEXT("Hidden relocation is cleared"), Boss->IsHiddenRelocationActive());
	TestFalse(TEXT("Dead boss cannot move on deck"), Boss->CanMoveOnDeck());
	TestTrue(TEXT("Occupied point is released for other actors"), Ship->IsDeckPointAvailable(701, nullptr));
	Boss->ApplyLocalDeathRagdoll();
	TestFalse(TEXT("Legacy ragdoll call cannot enable physics"), Boss->GetMesh()->IsSimulatingPhysics());
	TestEqual(TEXT("Capsule collision stays disabled"), Boss->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Dash damage sensor stays disabled"), Boss->GetDashDamageVolume()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	Boss->FinishHiddenRelocation();
	Boss->SetBossHidden(true);
	TestFalse(TEXT("Late Vanish callback cannot hide the corpse"), Boss->IsHidden());
	FindFProperty<FBoolProperty>(Boss->GetClass(), TEXT("bBossHidden"))->SetPropertyValue_InContainer(Boss, true);
	Boss->ProcessEvent(Boss->FindFunctionChecked(TEXT("OnRep_BossHidden")), nullptr);
	TestFalse(TEXT("Stale visibility replication cannot hide the corpse"), Boss->IsHidden());
	Boss->ProcessEvent(Boss->FindFunctionChecked(TEXT("OnRep_HostShip")), nullptr);
	TestEqual(TEXT("Late relocation cleanup cannot resume walking"), Boss->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestFalse(TEXT("Late teleport cannot move a dead boss"), Boss->RelocateWhileHidden(FTransform::Identity));
	TestNull(TEXT("CharacterMovement cannot double-apply ship motion"), Boss->GetMovementBase());

	Ship->SetActorTransform(FTransform(FRotator(17.0, 105.0, -13.0), FVector(620.0, -180.0, 95.0)));
	const FTransform ExpectedWorld = DeathLocal * Ship->GetShipDeckMesh()->GetComponentTransform();
	TestTrue(TEXT("Corpse follows ship translation, yaw, pitch and roll"), Boss->GetActorTransform().Equals(ExpectedWorld, 0.01f));
	TestTrue(TEXT("Existing five-second corpse lifespan remains"), FMath::IsNearlyEqual(Boss->GetLifeSpan(), 5.0f, 0.01f));
	Boss->GetHealthComponent()->StartDeath();
	Boss->GetHealthComponent()->FinishDeath();
	TestTrue(TEXT("Repeated death does not recapture the anchor"), Boss->GetRootComponent()->GetRelativeTransform().Equals(DeathLocal, 0.01f));
	// World ticks advance the actual lifespan timer, rather than testing a helper.
	for (int32 Frame = 0; Frame < 330 && !Boss->IsActorBeingDestroyed(); ++Frame)
	{
		++GFrameCounter;
		Scope.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}
	TestTrue(TEXT("Corpse is eventually destroyed"), Boss->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossDeathMontageTest,
	"ArtisticSW.Enemy.BossDeath.HeldMontageAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossDeathMontageTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UClass* BossClass = LoadObject<UClass>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Fab/Samurai/Animations/Montages/AM_Samurai_Death.AM_Samurai_Death"));
	if (!TestNotNull(TEXT("Authored boss loads"), BossClass) || !TestNotNull(TEXT("Boss death montage loads"), Montage))
	{
		return false;
	}
	const AShipBossEnemy* Defaults = BossClass->GetDefaultObject<AShipBossEnemy>();
	TestFalse(TEXT("Death montage holds the final pose"), Montage->bEnableAutoBlendOut);
	TestTrue(TEXT("Death montage has no premature completion notify"), Montage->Notifies.IsEmpty());
	TestEqual(TEXT("Death montage uses one linear section"), Montage->CompositeSections.Num(), 1);
	TestEqual(TEXT("Death section does not loop"), Montage->CompositeSections[0].NextSectionName, NAME_None);
	TestTrue(TEXT("Boss mesh supports death animation skeleton"), Defaults->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() == Montage->GetSkeleton());

	BossDeathTests::FWorldScope Scope;
	AEnemyShip* Ship = BossDeathTests::CreateShip(Scope.World);
	AShipBossEnemy* Boss = BossDeathTests::CreateBoss(Scope.World, BossClass);
	TestTrue(TEXT("Authored boss initializes"), Boss->InitializeBoss(Ship, 701, nullptr));
	Boss->GetHealthComponent()->StartDeath();
	TestEqual(TEXT("Montage keeps death in progress"), Boss->GetHealthComponent()->GetDeathState(), EBaseDeathState::DeathStarted);
	UAnimInstance* Anim = Boss->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("Boss AnimBP instance is available"), Anim))
	{
		return false;
	}
	TestTrue(TEXT("Dedicated Death GA starts the configured montage"), Anim->Montage_IsActive(Montage));
	TestEqual(TEXT("Corpse timer does not start before animation ends"), Boss->GetLifeSpan(), 0.0f);
	for (int32 Frame = 0; Frame < 185; ++Frame)
	{
		++GFrameCounter;
		// This isolated world has no game mode; explicitly advance animation as
		// well as world timers without relying on viewport visibility.
		Boss->GetMesh()->TickAnimation(1.0f / 60.0f, false);
		Boss->GetMesh()->RefreshBoneTransforms();
		Scope.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}
	TestEqual(TEXT("Non-blending montage still finishes death"), Boss->GetHealthComponent()->GetDeathState(), EBaseDeathState::DeathFinished);
	TestTrue(TEXT("Corpse remains visible during the existing delay"), !Boss->IsActorBeingDestroyed() && !Boss->IsHidden());
	TestTrue(TEXT("Held montage remains active after GA ends"), Anim->Montage_IsActive(Montage));
	TestTrue(TEXT("Held montage reached its terminal pose"), Anim->Montage_GetPosition(Montage) > Montage->GetPlayLength() - 0.05f);
	TestFalse(TEXT("No ragdoll after animation completes"), Boss->GetMesh()->IsSimulatingPhysics());
	TestTrue(TEXT("Five-second delay is counted from montage completion"), Boss->GetLifeSpan() > 4.5f && Boss->GetLifeSpan() <= 5.0f);
	for (int32 Frame = 0; Frame < 310 && !Boss->IsActorBeingDestroyed(); ++Frame)
	{
		++GFrameCounter;
		Scope.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}
	TestTrue(TEXT("Animated corpse is destroyed after the existing delay"), Boss->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossDeathMontageInterruptedTest,
	"ArtisticSW.Enemy.BossDeath.InterruptedMontageStillRetires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossDeathMontageInterruptedTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UClass* BossClass = LoadObject<UClass>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	if (!TestNotNull(TEXT("Boss Blueprint loads"), BossClass))
	{
		return false;
	}
	BossDeathTests::FWorldScope Scope;
	AEnemyShip* Ship = BossDeathTests::CreateShip(Scope.World);
	AShipBossEnemy* Boss = BossDeathTests::CreateBoss(Scope.World, BossClass);
	Boss->InitializeBoss(Ship, 701, nullptr);
	Boss->GetHealthComponent()->StartDeath();
	TestEqual(TEXT("Death animation starts"), Boss->GetHealthComponent()->GetDeathState(), EBaseDeathState::DeathStarted);
	Boss->GetAbilitySystemComponent()->CancelAllAbilities();
	TestEqual(TEXT("Cancellation finishes the death state"), Boss->GetHealthComponent()->GetDeathState(), EBaseDeathState::DeathFinished);
	TestTrue(TEXT("Cancellation still schedules the existing lifespan"), Boss->GetLifeSpan() > 0.0f);
	TestFalse(TEXT("Cancellation never falls back to ragdoll"), Boss->GetMesh()->IsSimulatingPhysics());
	TestTrue(TEXT("Cancellation keeps the deck anchor"), Boss->GetRootComponent()->GetAttachParent() == Ship->GetShipDeckMesh());
	Ship->Destroy();
	TestTrue(TEXT("Host destruction immediately removes the anchored corpse"), Boss->IsActorBeingDestroyed());
	return true;
}

#endif
