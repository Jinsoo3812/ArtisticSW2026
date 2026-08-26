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
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipBoardingPoint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipAuthoringComponentsTest,
	"ArtisticSW.Ship.Authoring.Components",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipAuthoringComponentsTest::RunTest(const FString& Parameters)
{
	UClass* PlayerShipClass = LoadClass<AShip>(
		nullptr,
		TEXT("/Game/New/Ship/Blueprints/BP_PlayerShip.BP_PlayerShip_C"));
	TestNotNull(TEXT("BP_PlayerShip loads against the new native component layout"), PlayerShipClass);
	if (PlayerShipClass)
	{
		const AShip* PlayerShipDefaults = PlayerShipClass->GetDefaultObject<AShip>();
		TestNotNull(TEXT("BP_PlayerShip inherits HelmInteractable"), PlayerShipDefaults->GetHelmInteractable());
		TestNotNull(TEXT("BP_PlayerShip inherits BoardingArrivalPoint"), PlayerShipDefaults->GetBoardingArrivalPoint());
		TestNotNull(TEXT("BP_PlayerShip inherits AnchorMesh"), PlayerShipDefaults->GetAnchorMesh());
		TestNotNull(TEXT("BP_PlayerShip inherits AnchorInteractable"), PlayerShipDefaults->GetAnchorInteractable());
	}

	UClass* CannonBlueprintClass = LoadClass<ACannon>(
		nullptr,
		TEXT("/Game/New/Cannon/BP_Cannon.BP_Cannon_C"));
	TestNotNull(TEXT("Canonical BP_Cannon loads"), CannonBlueprintClass);

	const AShip* ShipDefaults = GetDefault<AShip>();
	TestNotNull(TEXT("Helm interaction component exists"), ShipDefaults->GetHelmInteractable());
	TestNotNull(TEXT("Helm seat point exists"), ShipDefaults->GetHelmSeatPoint());
	TestNotNull(TEXT("Helm exit point exists"), ShipDefaults->GetHelmExitPoint());
	TestNotNull(TEXT("Shared boarding arrival point exists"), ShipDefaults->GetBoardingArrivalPoint());
	TestNotNull(TEXT("Anchor mesh exists"), ShipDefaults->GetAnchorMesh());
	TestNotNull(TEXT("Anchor interaction component exists"), ShipDefaults->GetAnchorInteractable());
	TestFalse(TEXT("Anchor is raised by default"), ShipDefaults->IsAnchorDropped());
	if (ShipDefaults->GetHelmInteractable())
	{
		TestEqual(
			TEXT("Helm interaction uses the ship-control gameplay tag by default"),
			ShipDefaults->GetHelmInteractable()->InteractionTag.ToString(),
			FString(TEXT("Interaction.ShipBoard")));
	}

	const AShipBoardingPoint* BoardingDefaults = GetDefault<AShipBoardingPoint>();
	TestNotNull(TEXT("Reusable boarding point owns an interaction component"), BoardingDefaults->GetBoardingInteractable());
	TestTrue(TEXT("Boarding interaction radius is designer editable and positive"), BoardingDefaults->InteractionSphereRadius > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipAnchorIntegrationTest,
	"ArtisticSW.Ship.Authoring.AnchorIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipAnchorIntegrationTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Recipe_DecipherCipher has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Recipe_DecipherCipher contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipAnchorTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AShip* Ship = World->SpawnActor<AShip>();
	if (!TestNotNull(TEXT("Ship is spawned"), Ship))
	{
		CleanupWorld();
		return false;
	}

	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	TestFalse(TEXT("Initial anchor state is raised"), Ship->IsAnchorDropped());
	TestNotNull(TEXT("Anchor interactable exists"), Ship->GetAnchorInteractable());

	Ship->ToggleAnchor();
	TestTrue(TEXT("Anchor is dropped after toggle"), Ship->IsAnchorDropped());

	Ship->ToggleAnchor();
	TestFalse(TEXT("Anchor is raised after second toggle"), Ship->IsAnchorDropped());

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipHelmAndCannonIntegrationTest,
	"ArtisticSW.Ship.Authoring.HelmBoardingAndCannonIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipHelmAndCannonIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipAuthoringTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AShip* Ship = World->SpawnActor<AShip>();
	APlayerController* FirstController = World->SpawnActor<APlayerController>();
	ACharacter* FirstPlayer = World->SpawnActor<ACharacter>();
	APlayerController* SecondController = World->SpawnActor<APlayerController>();
	ACharacter* SecondPlayer = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Ship is spawned"), Ship)
		|| !TestNotNull(TEXT("First controller is spawned"), FirstController)
		|| !TestNotNull(TEXT("First player is spawned"), FirstPlayer)
		|| !TestNotNull(TEXT("Second controller is spawned"), SecondController)
		|| !TestNotNull(TEXT("Second player is spawned"), SecondPlayer))
	{
		CleanupWorld();
		return false;
	}

	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	Ship->GetHelmSeatPoint()->SetRelativeLocation(FVector(120.0f, 30.0f, 80.0f));
	Ship->GetHelmExitPoint()->SetRelativeLocation(FVector(-150.0f, 90.0f, 60.0f));
	Ship->GetBoardingArrivalPoint()->SetRelativeLocation(FVector(0.0f, -200.0f, 75.0f));
	FirstController->Possess(FirstPlayer);
	SecondController->Possess(SecondPlayer);

	Ship->Board(FirstPlayer);
	TestTrue(TEXT("First player becomes the helmsman"), Ship->GetRidingPlayer() == FirstPlayer);
	TestTrue(TEXT("First controller possesses the ship"), FirstController->GetPawn() == Ship);
	TestTrue(TEXT("Helmsman is attached to the authored seat point"), FirstPlayer->GetAttachParentActor() == Ship);
	TestTrue(TEXT("Helmsman snaps to the seat location"), FirstPlayer->GetActorLocation().Equals(Ship->GetHelmSeatPoint()->GetComponentLocation(), 0.1f));
	TestEqual(TEXT("Occupied helm interaction is disabled"), Ship->GetHelmInteractable()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	AddExpectedError(
		TEXT("Ship is already being ridden"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	Ship->Board(SecondPlayer);
	TestTrue(TEXT("A second player cannot replace the active helmsman"), Ship->GetRidingPlayer() == FirstPlayer);
	TestTrue(TEXT("Second player keeps their controller"), SecondController->GetPawn() == SecondPlayer);

	Ship->ForceDisembark();
	TestNull(TEXT("Disembarking clears the helmsman"), Ship->GetRidingPlayer());
	TestTrue(TEXT("First controller repossesses the character"), FirstController->GetPawn() == FirstPlayer);
	TestTrue(TEXT("Player moves to the authored helm exit"), FirstPlayer->GetActorLocation().Equals(Ship->GetHelmExitPoint()->GetComponentLocation(), 0.1f));
	TestEqual(TEXT("Free helm interaction is re-enabled"), Ship->GetHelmInteractable()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);

	Ship->BoardFromSea(SecondPlayer);
	TestTrue(TEXT("Sea boarding uses the shared arrival point"), SecondPlayer->GetActorLocation().Equals(Ship->GetBoardingArrivalPoint()->GetComponentLocation(), 0.1f));

	UChildActorComponent* CannonSlot = NewObject<UChildActorComponent>(Ship, TEXT("AutomationCannonSlot"));
	Ship->AddInstanceComponent(CannonSlot);
	CannonSlot->SetupAttachment(Ship->BuoyancyRoot);
	UClass* CannonBlueprintClass = LoadClass<ACannon>(
		nullptr,
		TEXT("/Game/New/Cannon/BP_Cannon.BP_Cannon_C"));
	TestNotNull(TEXT("Canonical BP_Cannon class loads for the child slot"), CannonBlueprintClass);
	CannonSlot->SetChildActorClass(CannonBlueprintClass ? CannonBlueprintClass : ACannon::StaticClass());
	CannonSlot->RegisterComponent();
	Ship->RefreshMountedCannons();
	TestEqual(TEXT("Ship discovers BP-style cannon child actor slots"), Ship->GetMountedCannonCount(), 1);

	ACannon* MountedCannon = Cast<ACannon>(CannonSlot->GetChildActor());
	TestNotNull(TEXT("Cannon child actor is created"), MountedCannon);
	if (MountedCannon)
	{
		TestTrue(TEXT("Mounted cannon resolves its owning ship"), MountedCannon->GetOwningShip() == Ship);

		Ship->GetAbilitySystemComponent()->AddSpawnedAttribute(Ship->GetShipAttributeSet());
		Ship->GetAbilitySystemComponent()->InitAbilityActorInfo(Ship, Ship);
		FShipStatSnapshot Stats;
		Stats.MaxHealth = 250.0f;
		Stats.CannonDamage = 47.0f;
		Stats.CannonFireCooldownSeconds = 0.8f;
		Stats.CannonballSpeed = 4321.0f;
		Ship->ApplyStatSnapshot(Stats, true);
		const FCannonResolvedFiringStats Resolved = MountedCannon->GetResolvedFiringStats();
		TestEqual(TEXT("Mounted cannon inherits ship damage attribute"), Resolved.Damage, 47.0f);
		TestEqual(TEXT("Mounted cannon inherits ship cooldown attribute"), Resolved.CooldownSeconds, 0.8f);
		TestEqual(TEXT("Mounted cannon inherits ship projectile speed attribute"), Resolved.ProjectileSpeed, 4321.0f);
	}

	AShipBoardingPoint* BoardingPoint = World->SpawnActor<AShipBoardingPoint>();
	TestNotNull(TEXT("Reusable boarding point is spawned"), BoardingPoint);
	if (BoardingPoint)
	{
		BoardingPoint->AttachToActor(Ship, FAttachmentTransformRules::KeepRelativeTransform);
		TestTrue(TEXT("Boarding point resolves the ship through attachment"), BoardingPoint->GetOwningShip() == Ship);
	}

	CleanupWorld();
	return true;
}

#endif
