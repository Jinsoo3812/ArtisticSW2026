#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Cannon.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InteractableComponent.h"
#include "ShipAttributeSet.h"
#include "ShipAI/EnemyShip.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipCannonIntegrationTest,
	"ArtisticSW.Enemy.Ship.CannonRegistryAndControlPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipCannonIntegrationTest::RunTest(const FString& Parameters)
{
	// The project-wide ItemSubsystem validates its current quest recipe while a game world is created.
	AddExpectedError(TEXT("QuestItem"), EAutomationExpectedErrorFlags::Contains, 3);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipCannonIntegrationWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	const auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyShip* EnemyShip = World->SpawnActor<AEnemyShip>();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	ACharacter* PlayerCharacter = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Enemy ship is spawned"), EnemyShip)
		|| !TestNotNull(TEXT("Player controller is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player character is spawned"), PlayerCharacter))
	{
		CleanupWorld();
		return false;
	}

	EnemyShip->BuoyancyRoot->SetSimulatePhysics(false);
	PlayerController->Possess(PlayerCharacter);
	TestFalse(TEXT("Enemy ship rejects player helm control"), EnemyShip->AllowsPlayerHelmControl());
	TestFalse(TEXT("Enemy ship rejects player cannon control"), EnemyShip->AllowsPlayerCannonControl());
	TestFalse(TEXT("Enemy ship rejects player sea boarding"), EnemyShip->AllowsPlayerBoarding());
	const FVector BeforeBoardingLocation = PlayerCharacter->GetActorLocation();
	EnemyShip->BoardFromSea(PlayerCharacter);
	TestTrue(
		TEXT("Rejected sea boarding does not teleport the player"),
		PlayerCharacter->GetActorLocation().Equals(BeforeBoardingLocation));

	AddExpectedError(
		TEXT("player helm control is disabled"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	EnemyShip->Board(PlayerCharacter);
	TestNull(TEXT("Rejected helm boarding does not set a rider"), EnemyShip->GetRidingPlayer());
	TestTrue(TEXT("Rejected helm boarding preserves player possession"), PlayerController->GetPawn() == PlayerCharacter);

	UChildActorComponent* CannonSlot = NewObject<UChildActorComponent>(EnemyShip, TEXT("AutomationEnemyCannonSlot"));
	EnemyShip->AddInstanceComponent(CannonSlot);
	CannonSlot->SetupAttachment(EnemyShip->BuoyancyRoot);
	CannonSlot->SetChildActorClass(ACannon::StaticClass());
	CannonSlot->RegisterComponent();

	ACannon* ChildCannon = Cast<ACannon>(CannonSlot->GetChildActor());
	ACannon* LegacyAttachedCannon = World->SpawnActor<ACannon>();
	if (!TestNotNull(TEXT("BP-style child cannon is created"), ChildCannon)
		|| !TestNotNull(TEXT("Legacy attached cannon is spawned"), LegacyAttachedCannon))
	{
		CleanupWorld();
		return false;
	}
	LegacyAttachedCannon->AttachToActor(EnemyShip, FAttachmentTransformRules::KeepRelativeTransform);

	EnemyShip->RefreshMountedCannons();
	TestEqual(TEXT("Canonical registry contains child and legacy cannons once each"), EnemyShip->GetMountedCannonCount(), 2);
	TestTrue(TEXT("Canonical registry contains the child cannon"), EnemyShip->GetMountedCannons().Contains(ChildCannon));
	TestTrue(TEXT("Canonical registry contains the legacy cannon"), EnemyShip->GetMountedCannons().Contains(LegacyAttachedCannon));
	TestTrue(TEXT("Child cannon resolves EnemyShip ownership"), ChildCannon->GetOwningShip() == EnemyShip);
	TestTrue(TEXT("Legacy cannon resolves EnemyShip ownership"), LegacyAttachedCannon->GetOwningShip() == EnemyShip);

	for (ACannon* Cannon : EnemyShip->GetMountedCannons())
	{
		if (!TestNotNull(TEXT("Registered cannon remains valid"), Cannon))
		{
			continue;
		}

		TestFalse(TEXT("Enemy-mounted cannon rejects player control"), Cannon->AllowsPlayerControl());
		if (UInteractableComponent* Interactable = Cannon->GetInteractableComponent())
		{
			TestEqual(
				TEXT("Enemy-mounted cannon interaction collision is disabled"),
				Interactable->GetCollisionEnabled(),
				ECollisionEnabled::NoCollision);
		}
	}

	AddExpectedError(
		TEXT("player control is disabled"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	ChildCannon->Board(PlayerCharacter);
	TestNull(TEXT("Rejected cannon boarding does not set a rider"), ChildCannon->GetRidingPlayer());
	TestTrue(TEXT("Rejected cannon boarding preserves player possession"), PlayerController->GetPawn() == PlayerCharacter);

	UAbilitySystemComponent* ShipASC = EnemyShip->GetAbilitySystemComponent();
	if (TestNotNull(TEXT("Enemy ship ASC exists"), ShipASC))
	{
		ShipASC->AddSpawnedAttribute(EnemyShip->GetShipAttributeSet());
		ShipASC->InitAbilityActorInfo(EnemyShip, EnemyShip);
		FShipStatSnapshot Stats;
		Stats.CannonDamage = 37.0f;
		Stats.CannonFireCooldownSeconds = 0.75f;
		Stats.CannonballSpeed = 4100.0f;
		EnemyShip->ApplyStatSnapshot(Stats, true);

		const FCannonResolvedFiringStats Resolved = ChildCannon->GetResolvedFiringStats();
		TestEqual(TEXT("Enemy cannon resolves ship damage"), Resolved.Damage, 37.0f);
		TestEqual(TEXT("Enemy cannon resolves ship cooldown"), Resolved.CooldownSeconds, 0.75f);
		TestEqual(TEXT("Enemy cannon resolves ship projectile speed"), Resolved.ProjectileSpeed, 4100.0f);
	}

	CleanupWorld();
	return true;
}

#endif
