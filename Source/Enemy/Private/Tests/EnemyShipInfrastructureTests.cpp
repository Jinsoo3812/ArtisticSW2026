#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AbilitySystemComponent.h"
#include "ShipAttributeSet.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "BaseGameplayTags.h"
#include "ShipAI/NavalAIController.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "BaseEnemy.h"
#include "RangedEnemy/RangedEnemy.h"

namespace EnemyShipInfrastructureTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipInfrastructureWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipNavigationOverrideTest,
	"ArtisticSW.Enemy.Ship.Navigation.OverridePriorityAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipNavigationOverrideTest::RunTest(const FString& Parameters)
{
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent();
	if (!TestNotNull(TEXT("Navigation Component exists"), Navigation))
	{
		return false;
	}

	FEnemyShipNavigationOverrideRequest ChargeRequest;
	ChargeRequest.MoveInput = 1.0f;
	ChargeRequest.PropulsionMultiplier = 3.0f;
	const FEnemyShipNavigationOverrideHandle ChargeHandle = Navigation->AcquireOverride(Ship, 10, ChargeRequest);
	TestTrue(TEXT("Charge override handle is valid"), ChargeHandle.IsValid());
	Navigation->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Charge scale survives normalized input clamping"), Ship->GetCurrentAIPropulsionScale(), 3.0f);

	FEnemyShipNavigationOverrideRequest StopRequest;
	StopRequest.Mode = EEnemyShipNavigationOverrideMode::StopMovement;
	const FEnemyShipNavigationOverrideHandle StopHandle = Navigation->AcquireOverride(Ship, 100, StopRequest);
	Navigation->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Higher-priority stop resets force scale"), Ship->GetCurrentAIPropulsionScale(), 1.0f);

	TestTrue(TEXT("Stop override releases"), Navigation->ReleaseOverride(StopHandle));
	Navigation->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Charge resumes after higher-priority override releases"), Ship->GetCurrentAIPropulsionScale(), 3.0f);
	TestTrue(TEXT("Charge override releases"), Navigation->ReleaseOverride(ChargeHandle));
	Navigation->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Normal navigation restores force scale"), Ship->GetCurrentAIPropulsionScale(), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipSkillRuntimeOneShotTest,
	"ArtisticSW.Enemy.Ship.SkillRuntime.OneShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipSkillRuntimeOneShotTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	AShip* Target = TestWorld.World->SpawnActor<AShip>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship) || !TestNotNull(TEXT("Target Ship spawned"), Target))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	Target->BuoyancyRoot->SetSimulatePhysics(false);
	Target->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Ship->GetAbilitySystemComponent()->InitAbilityActorInfo(Ship, Ship);
	TestTrue(
		TEXT("Pattern test ability is granted"),
		Ship->GrantEnemyShipAbilityClasses({UGA_EnemyShipCharge::StaticClass()}));

	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	UEnemyShipSkillModuleData* Module = NewObject<UEnemyShipSkillModuleData>();
	Module->AbilityClass = UGA_EnemyShipCharge::StaticClass();
	Module->Priority = 10;
	Archetype->SkillModules.Add(Module);

	UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent();
	Runtime->Configure(Archetype);
	FEnemyShipAbilitySelection Selection;
	TestTrue(TEXT("Skill is initially eligible"), Runtime->SelectAbilityAtTime(Target, 0.0, Selection));
	TestTrue(TEXT("Initial selection commits"), Runtime->CommitSelection(Selection));

	Module->bUseOnlyOnce = true;
	Runtime->Configure(Archetype);
	TestTrue(TEXT("One-shot skill selects once"), Runtime->SelectAbilityAtTime(Target, 10.0, Selection));
	TestTrue(TEXT("One-shot selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("One-shot skill cannot select again"), Runtime->SelectAbilityAtTime(Target, 100.0, Selection));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipArchetypeAssemblyTest,
	"ArtisticSW.Enemy.Ship.Data.ArchetypeAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipArchetypeAssemblyTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);

	UDataTable* SpecTable = NewObject<UDataTable>();
	SpecTable->RowStruct = FShipStatRow::StaticStruct();
	FShipStatRow Spec;
	Spec.MaxHealth = 450.0f;
	Spec.ForwardPropulsionMultiplier = 1.7f;
	Spec.TurnTorqueMultiplier = 1.2f;
	Spec.CannonDamage = 65.0f;
	Spec.CannonFireCooldown = 0.8f;
	Spec.CannonballSpeed = 4200.0f;
	SpecTable->AddRow(TEXT("SpecC"), Spec);

	UEnemyShipSkillModuleData* Module = NewObject<UEnemyShipSkillModuleData>();
	Module->AbilityClass = UGA_EnemyShipCharge::StaticClass();
	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	Archetype->SpecRow.DataTable = SpecTable;
	Archetype->SpecRow.RowName = TEXT("SpecC");
	Archetype->NavigationProfile.IdealDistance = 3300.0f;
	Archetype->SkillModules.Add(Module);

	TestTrue(TEXT("Archetype applies"), Archetype->ApplyToShip(Ship));
	const UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent();
	TestEqual(TEXT("Spec health applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetMaxHealthAttribute()), 450.0f);
	TestEqual(TEXT("Spec cannon damage applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute()), 65.0f);
	TestEqual(TEXT("Spec projectile speed applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetCannonballSpeedAttribute()), 4200.0f);
	TestEqual(TEXT("Archetype supplies ideal distance"), Ship->GetNavigationComponent()->GetNavigationProfile().IdealDistance, 3300.0f);
	TestFalse(TEXT("Runtime orbit is counterclockwise"), Ship->GetNavigationComponent()->GetNavigationProfile().bOrbitClockwise);
	TestEqual(TEXT("Archetype source ideal distance remains immutable"), Archetype->NavigationProfile.IdealDistance, 3300.0f);
	TestEqual(TEXT("Archetype runtime resolves its skill"), Ship->GetPatternRuntimeComponent()->GetResolvedRuleCount(), 1);
	TestEqual(TEXT("Full-health cannon cooldown multiplier is one"), Ship->GetCannonCooldownMultiplier(), 1.0f);
	Ship->GetShipAttributeSet()->InitHealth(225.0f);
	TestEqual(TEXT("Half-health cannon cooldown multiplier interpolates"), Ship->GetCannonCooldownMultiplier(), 2.0f);
	Ship->GetShipAttributeSet()->InitHealth(45.0f);
	TestEqual(TEXT("Low-health cannon cooldown multiplier interpolates toward three"), Ship->GetCannonCooldownMultiplier(), 2.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipSquadOrbitDistanceTest,
	"ArtisticSW.Enemy.Ship.Navigation.SquadOrbitDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipSquadOrbitDistanceTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	UShipSwarmSubsystem* Swarm = TestWorld.World->GetSubsystem<UShipSwarmSubsystem>();
	if (!TestNotNull(TEXT("Swarm subsystem exists"), Swarm))
	{
		return false;
	}

	TArray<AEnemyShip*> Ships;
	TArray<UEnemyShipArchetypeData*> Archetypes;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
		if (!TestNotNull(TEXT("Enemy ship spawned"), Ship))
		{
			return false;
		}
		Ship->BuoyancyRoot->SetSimulatePhysics(false);
		Swarm->UnregisterShip(Ship);
		Ship->SquadID = TEXT("OrbitDistanceTest");
		UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
		Archetype->NavigationProfile.IdealDistance = 28000.0f + Index * 1000.0f;
		Archetype->OrbitDistanceSpacing = 2000.0f + Index * 500.0f;
		TestTrue(TEXT("Archetype configures"), Ship->ConfigureEnemyShipArchetype(Archetype));
		Swarm->RegisterShip(Ship);
		Ships.Add(Ship);
		Archetypes.Add(Archetype);
	}

	Swarm->RecalculateSquadOrbitDistances(TEXT("OrbitDistanceTest"));
	Ships.Sort([](const AEnemyShip& Left, const AEnemyShip& Right)
	{
		return Left.GetFName().LexicalLess(Right.GetFName());
	});
	const float ExpectedDistances[] = {24000.0f, 27000.0f, 30000.0f, 33000.0f, 36000.0f};
	for (int32 Index = 0; Index < Ships.Num(); ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Ship %d receives its symmetric orbit lane"), Index),
			Ships[Index]->GetNavigationComponent()->GetNavigationProfile().IdealDistance,
			ExpectedDistances[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipControllerTargetRoutingTest,
	"ArtisticSW.Enemy.Ship.Controller.TargetRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipControllerTargetRoutingTest::RunTest(const FString& Parameters)
{
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	AShip* PlayerShip = TestWorld.World->SpawnActor<AShip>();
	ANavalAIController* Controller = TestWorld.World->SpawnActor<ANavalAIController>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship)
		|| !TestNotNull(TEXT("Player Ship spawned"), PlayerShip)
		|| !TestNotNull(TEXT("Naval Controller spawned"), Controller))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	PlayerShip->BuoyancyRoot->SetSimulatePhysics(false);
	FEnemyShipNavigationProfile NavigationProfile = Ship->GetNavigationComponent()->GetNavigationProfile();
	NavigationProfile.DetectionDistance = 1500.0f;
	Ship->GetNavigationComponent()->SetNavigationProfile(NavigationProfile);
	PlayerShip->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Controller->Possess(Ship);
	Controller->RefreshTargetShip();
	TestTrue(TEXT("Controller routes closest Player Ship to Navigation"), Ship->GetNavigationComponent()->GetTargetShip() == PlayerShip);

	PlayerShip->SetActorLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestTrue(
		TEXT("Authored Player Ship is outside the configured detection radius"),
		FVector::Dist2D(Ship->GetActorLocation(), PlayerShip->GetActorLocation())
			> Ship->GetNavigationComponent()->GetNavigationProfile().DetectionDistance);
	Controller->RefreshTargetShip();
	TestTrue(TEXT("Controller no longer owns the out-of-range authored target"), Controller->GetTargetShip() != PlayerShip);
	TestTrue(
		TEXT("Controller no longer routes the out-of-range authored target"),
		Ship->GetNavigationComponent()->GetTargetShip() != PlayerShip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipDeterministicAvoidanceTest,
	"ArtisticSW.Enemy.Ship.Navigation.DeterministicAvoidance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipDeterministicAvoidanceTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	UShipSwarmSubsystem* Swarm = TestWorld.World->GetSubsystem<UShipSwarmSubsystem>();
	AEnemyShip* First = TestWorld.World->SpawnActor<AEnemyShip>();
	AEnemyShip* Second = TestWorld.World->SpawnActor<AEnemyShip>();
	AShip* Target = TestWorld.World->SpawnActor<AShip>();
	if (!TestNotNull(TEXT("Swarm exists"), Swarm)
		|| !TestNotNull(TEXT("First ship spawned"), First)
		|| !TestNotNull(TEXT("Second ship spawned"), Second)
		|| !TestNotNull(TEXT("Target spawned"), Target))
	{
		return false;
	}

	First->DispatchBeginPlay();
	Second->DispatchBeginPlay();
	Target->DispatchBeginPlay();
	First->BuoyancyRoot->SetSimulatePhysics(false);
	Second->BuoyancyRoot->SetSimulatePhysics(false);
	Target->BuoyancyRoot->SetSimulatePhysics(false);
	First->SetActorLocation(FVector::ZeroVector);
	Second->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	Target->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	Swarm->UnregisterShip(First);
	Swarm->UnregisterShip(Second);
	First->SquadID = TEXT("AvoidanceTest");
	Second->SquadID = TEXT("AvoidanceTest");
	First->GetNavigationComponent()->SetTargetShip(Target);
	Second->GetNavigationComponent()->SetTargetShip(Target);
	First->GetNavigationComponent()->SetNavigationEnabled(true);
	Second->GetNavigationComponent()->SetNavigationEnabled(true);
	Swarm->RegisterShip(First);
	Swarm->RegisterShip(Second);

	AEnemyShip* YieldingShip = First->GetFName().LexicalLess(Second->GetFName()) ? Second : First;
	AEnemyShip* PriorityShip = YieldingShip == First ? Second : First;
	TestTrue(TEXT("Exactly the lower-priority ship predicts a yield"), Swarm->EvaluateAvoidance(YieldingShip).bShouldYield);
	TestFalse(TEXT("Priority ship does not reciprocally yield"), Swarm->EvaluateAvoidance(PriorityShip).bShouldYield);
	FEnemyShipNavigationOverrideRequest ChargeLikeRequest;
	ChargeLikeRequest.MoveInput = 1.0f;
	const FEnemyShipNavigationOverrideHandle ChargeLikeHandle =
		YieldingShip->GetNavigationComponent()->AcquireOverride(YieldingShip, 100, ChargeLikeRequest);
	TestTrue(TEXT("Movement override receives absolute right of way"), Swarm->EvaluateAvoidance(PriorityShip).bShouldYield);
	YieldingShip->GetNavigationComponent()->ReleaseOverride(ChargeLikeHandle);

	YieldingShip->GetNavigationComponent()->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Avoidance maneuver latches"), YieldingShip->GetNavigationComponent()->IsAvoidanceManeuverActive());
	TestTrue(TEXT("Yielding ship applies reverse thrust"), YieldingShip->GetCurrentMoveInput() < 0.0f);

	FEnemyShipNavigationProfile ReturnProfile = First->GetNavigationComponent()->GetNavigationProfile();
	ReturnProfile.ReturnArrivalDistance = 100.0f;
	ReturnProfile.ReturnTriggerDistance = 1000.0f;
	ReturnProfile.LostTargetReturnDelay = 0.0f;
	First->GetNavigationComponent()->SetNavigationProfile(ReturnProfile);
	Second->GetNavigationComponent()->SetNavigationProfile(ReturnProfile);
	First->GetNavigationComponent()->SetTargetShip(nullptr);
	Second->GetNavigationComponent()->SetTargetShip(nullptr);
	First->GetNavigationComponent()->SetNavigationEnabled(false);
	First->GetNavigationComponent()->SetNavigationEnabled(true);
	FVector FirstHome = FVector::ZeroVector;
	TestTrue(
		TEXT("Returning ship captured its spawn home"),
		First->GetNavigationComponent()->GetResolvedHomeLocation(FirstHome));
	FVector SecondHome = FVector::ZeroVector;
	TestTrue(
		TEXT("Stopped ship captured its spawn home"),
		Second->GetNavigationComponent()->GetResolvedHomeLocation(SecondHome));
	First->SetActorLocation(FirstHome + FVector(10000.0f, 0.0f, 0.0f));
	Second->GetNavigationComponent()->SetNavigationEnabled(true);
	Second->SetActorLocation(SecondHome);
	Second->GetNavigationComponent()->TickComponent(0.25f, LEVELTICK_All, nullptr);
	TestEqual(
		TEXT("Squadmate is Idle at its completed return point"),
		Second->GetNavigationComponent()->GetCurrentState(),
		ENavalCombatState::Idle);
	Second->GetNavigationComponent()->SetNavigationEnabled(false);
	First->SetActorLocation(FirstHome + FVector(5000.0f, 0.0f, 0.0f));
	Second->SetActorLocation(FirstHome + FVector(5000.0f, 0.0f, 0.0f));
	TestEqual(TEXT("Return test keeps both squad members registered"), Swarm->GetSquadMembers(First->SquadID).Num(), 2);
	TestTrue(
		TEXT("Return test ships are within hull avoidance range"),
		FVector::Dist2D(First->GetActorLocation(), Second->GetActorLocation()) < 1.0f);
	TestFalse(TEXT("Stopped return obstacle remains alive"), Second->IsDeathHandled());
	First->GetNavigationComponent()->TickComponent(0.25f, LEVELTICK_All, nullptr);
	TestEqual(
		TEXT("Targetless ship enters Return"),
		First->GetNavigationComponent()->GetCurrentState(),
		ENavalCombatState::Return);
	TestTrue(
		TEXT("Return prediction includes a stopped targetless squadmate"),
		Swarm->EvaluateAvoidance(First).bShouldYield);
	TestTrue(
		TEXT("Returning ship avoids a stopped targetless squadmate"),
		First->GetNavigationComponent()->IsAvoidanceManeuverActive());

	Second->SetActorLocation(FirstHome);
	TestFalse(TEXT("Occupied return transform blocks completion"), Swarm->IsReturnDestinationClear(First));
	Second->SetActorLocation(FirstHome + FVector(10000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Separated return transform permits completion"), Swarm->IsReturnDestinationClear(First));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipAnchorIntegrationTest,
	"ArtisticSW.Enemy.Ship.Anchor.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipAnchorIntegrationTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Recipe_DecipherCipher"), EAutomationExpectedErrorFlags::Contains, 3);

	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	Ship->SetActorLocation(FVector(1500.0f, 2500.0f, 0.0f));

	TestNotNull(TEXT("Anchor Mesh exists on Enemy Ship"), Ship->GetAnchorMesh());
	TestNotNull(TEXT("Anchor Interactable exists on Enemy Ship"), Ship->GetAnchorInteractable());
	TestFalse(TEXT("Anchor is raised by default"), Ship->IsAnchorDropped());

	// Drop anchor
	Ship->ToggleAnchor();
	TestTrue(TEXT("Anchor is dropped after toggle"), Ship->IsAnchorDropped());
	TestEqual(TEXT("Anchor origin X matches actor location"), Ship->GetAnchorOriginXY().X, 1500.0);
	TestEqual(TEXT("Anchor origin Y matches actor location"), Ship->GetAnchorOriginXY().Y, 2500.0);

	// Verify that SetAIControlInput and Navigation overrides are suppressed when anchored
	Ship->SetAIControlInput(1.0f, 0.8f);
	TestEqual(TEXT("Move input suppressed to 0 while anchored"), Ship->GetCurrentMoveInput(), 0.0f);
	TestEqual(TEXT("Turn input suppressed to 0 while anchored"), Ship->GetCurrentTurnInput(), 0.0f);

	UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent();
	if (TestNotNull(TEXT("Navigation Component exists"), Navigation))
	{
		Navigation->SetNavigationEnabled(true);
		FEnemyShipNavigationOverrideRequest Request;
		Request.MoveInput = 1.0f;
		Request.TurnInput = 0.5f;
		const FEnemyShipNavigationOverrideHandle OverrideHandle = Navigation->AcquireOverride(Ship, 50, Request);
		TestTrue(TEXT("Override acquired"), OverrideHandle.IsValid());

		Navigation->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Move input remains 0 on tick while anchored"), Ship->GetCurrentMoveInput(), 0.0f);
		TestEqual(TEXT("Turn input remains 0 on tick while anchored"), Ship->GetCurrentTurnInput(), 0.0f);

		// Raise anchor
		Ship->ToggleAnchor();
		TestFalse(TEXT("Anchor is raised after second toggle"), Ship->IsAnchorDropped());

		Navigation->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Move input applied after anchor raised"), Ship->GetCurrentMoveInput(), 1.0f);
		TestEqual(TEXT("Turn input applied after anchor raised"), Ship->GetCurrentTurnInput(), 0.5f);

		Navigation->ReleaseOverride(OverrideHandle);
	}
	else
	{
		// Raise anchor
		Ship->ToggleAnchor();
		TestFalse(TEXT("Anchor is raised after second toggle"), Ship->IsAnchorDropped());

		Ship->SetAIControlInput(1.0f, 0.8f);
		TestEqual(TEXT("Move input applied when anchor raised"), Ship->GetCurrentMoveInput(), 1.0f);
		TestEqual(TEXT("Turn input applied when anchor raised"), Ship->GetCurrentTurnInput(), 0.8f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipCrewGatedAnchorTest,
	"ArtisticSW.Enemy.Ship.Anchor.CrewGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipCrewGatedAnchorTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem"), EAutomationExpectedErrorFlags::Contains, 3);

	EnemyShipInfrastructureTests::FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	ARangedEnemy* CrewMember = TestWorld.World->SpawnActor<ARangedEnemy>();
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship)
		|| !TestNotNull(TEXT("Crew Member spawned"), CrewMember))
	{
		return false;
	}
	Ship->BuoyancyRoot->SetSimulatePhysics(false);

	// Initially without crew
	TestFalse(TEXT("Initially no living crew"), Ship->HasLivingCrew());
	TestEqual(TEXT("Living crew count is 0"), Ship->GetLivingCrewCount(), 0);
	TestFalse(TEXT("Anchor control remains locked before a crew encounter"), Ship->AllowsPlayerAnchorControl());
	TestFalse(TEXT("Helm control remains locked before a crew encounter"), Ship->AllowsPlayerHelmControl());

	// Register crew member
	Ship->RegisterCrewEnemy(CrewMember);
	TestTrue(TEXT("Has living crew after registration"), Ship->HasLivingCrew());
	TestEqual(TEXT("Living crew count is 1"), Ship->GetLivingCrewCount(), 1);
	TestFalse(TEXT("Anchor control blocked while crew alive"), Ship->AllowsPlayerAnchorControl());
	TestFalse(TEXT("Helm control blocked while crew alive"), Ship->AllowsPlayerHelmControl());

	// Attempt anchor interaction while crew alive
	AddExpectedError(TEXT("Anchor control rejected on"), EAutomationExpectedErrorFlags::Contains, 1);
	Ship->HandleAnchorInteracted(nullptr);
	TestFalse(TEXT("Anchor remains raised while crew alive"), Ship->IsAnchorDropped());

	// Unregister or eliminate crew
	Ship->UnregisterCrewEnemy(CrewMember);
	TestFalse(TEXT("No living crew after unregistering"), Ship->HasLivingCrew());
	TestEqual(TEXT("Living crew count is 0"), Ship->GetLivingCrewCount(), 0);
	TestTrue(TEXT("Anchor control allowed after crew eliminated"), Ship->AllowsPlayerAnchorControl());
	TestTrue(TEXT("Helm control allowed after crew eliminated"), Ship->AllowsPlayerHelmControl());

	// Interaction now succeeds
	Ship->HandleAnchorInteracted(nullptr);
	TestTrue(TEXT("Anchor drops after crew eliminated"), Ship->IsAnchorDropped());

	return true;
}

#endif
