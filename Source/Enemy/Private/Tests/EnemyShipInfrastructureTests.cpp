#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AbilitySystemComponent.h"
#include "ShipAttributeSet.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
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
	FEnemyShipPatternRuntimeIntervalTest,
	"ArtisticSW.Enemy.Ship.Pattern.IntervalAndOneShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipPatternRuntimeIntervalTest::RunTest(const FString& Parameters)
{
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

	UEnemyShipPatternData* Pattern = NewObject<UEnemyShipPatternData>();
	UEnemyShipAbilitySet* AbilitySet = NewObject<UEnemyShipAbilitySet>();
	AbilitySet->Abilities.Add(UGA_EnemyShipCharge::StaticClass());
	UEnemyShipSkillModuleData* Module = NewObject<UEnemyShipSkillModuleData>();
	Module->ModuleId = TEXT("IntervalTest");
	Module->AbilitySet = AbilitySet;
	FEnemyShipSkillRule& RepeatRule = Module->SkillRules.AddDefaulted_GetRef();
	RepeatRule.RuleId = TEXT("IntervalCharge");
	RepeatRule.AbilityTag = GameplayAbility_EnemyShip_Charge;
	RepeatRule.AbilityClass = UGA_EnemyShipCharge::StaticClass();
	RepeatRule.MinimumInterval = 5.0f;
	RepeatRule.MaximumDistance = 2000.0f;
	RepeatRule.Priority = 10;
	Pattern->SkillModules.Add(Module);

	UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent();
	Runtime->SetPattern(Pattern);
	FEnemyShipAbilitySelection Selection;
	TestTrue(TEXT("Rule is initially eligible"), Runtime->SelectAbilityAtTime(Target, 0.0, Selection));
	TestTrue(TEXT("Initial selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("Rule is blocked before interval"), Runtime->SelectAbilityAtTime(Target, 4.99, Selection));
	TestTrue(TEXT("Rule is eligible at interval boundary"), Runtime->SelectAbilityAtTime(Target, 5.0, Selection));

	Module->SkillRules[0].bUseOnlyOnce = true;
	Runtime->SetPattern(Pattern);
	TestTrue(TEXT("One-shot rule selects once"), Runtime->SelectAbilityAtTime(Target, 10.0, Selection));
	TestTrue(TEXT("One-shot selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("One-shot rule cannot select again"), Runtime->SelectAbilityAtTime(Target, 100.0, Selection));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipArchetypeAssemblyTest,
	"ArtisticSW.Enemy.Ship.Data.ArchetypeAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipArchetypeAssemblyTest::RunTest(const FString& Parameters)
{
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

	UEnemyShipPatternData* Pattern = NewObject<UEnemyShipPatternData>();
	Pattern->NavigationProfile.IdealDistance = 3300.0f;
	Pattern->NavigationProfile.MaxActiveCannons = 4;
	UEnemyShipAbilitySet* AbilitySet = NewObject<UEnemyShipAbilitySet>();
	AbilitySet->Abilities.Add(UGA_EnemyShipCharge::StaticClass());
	UEnemyShipSkillModuleData* Module = NewObject<UEnemyShipSkillModuleData>();
	Module->ModuleId = TEXT("AssemblyCharge");
	Module->AbilitySet = AbilitySet;
	FEnemyShipSkillRule& Rule = Module->SkillRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("AssemblyChargeRule");
	Rule.AbilityTag = GameplayAbility_EnemyShip_Charge;
	Rule.AbilityClass = UGA_EnemyShipCharge::StaticClass();
	Pattern->SkillModules.Add(Module);
	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	Archetype->SpecRow.DataTable = SpecTable;
	Archetype->SpecRow.RowName = TEXT("SpecC");
	Archetype->Pattern = Pattern;

	TestTrue(TEXT("Archetype applies"), Archetype->ApplyToShip(Ship));
	const UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent();
	TestEqual(TEXT("Spec health applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetMaxHealthAttribute()), 450.0f);
	TestEqual(TEXT("Spec cannon damage applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute()), 65.0f);
	TestEqual(TEXT("Spec projectile speed applies"), ASC->GetNumericAttribute(UShipAttributeSet::GetCannonballSpeedAttribute()), 4200.0f);
	TestEqual(TEXT("Pattern navigation applies"), Ship->GetNavigationComponent()->GetNavigationProfile().IdealDistance, 3300.0f);
	TestTrue(TEXT("Pattern runtime uses the same immutable Pattern asset"), Ship->GetPatternRuntimeComponent()->GetPattern() == Pattern);
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
	AddExpectedError(TEXT("Recipe_DecipherCipher"), EAutomationExpectedErrorFlags::Contains, 3);

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
	TestTrue(TEXT("Anchor control allowed without crew"), Ship->AllowsPlayerAnchorControl());

	// Register crew member
	Ship->RegisterCrewEnemy(CrewMember);
	TestTrue(TEXT("Has living crew after registration"), Ship->HasLivingCrew());
	TestEqual(TEXT("Living crew count is 1"), Ship->GetLivingCrewCount(), 1);
	TestFalse(TEXT("Anchor control blocked while crew alive"), Ship->AllowsPlayerAnchorControl());

	// Attempt anchor interaction while crew alive
	AddExpectedError(TEXT("Anchor control rejected on"), EAutomationExpectedErrorFlags::Contains, 1);
	Ship->HandleAnchorInteracted(nullptr);
	TestFalse(TEXT("Anchor remains raised while crew alive"), Ship->IsAnchorDropped());

	// Unregister or eliminate crew
	Ship->UnregisterCrewEnemy(CrewMember);
	TestFalse(TEXT("No living crew after unregistering"), Ship->HasLivingCrew());
	TestEqual(TEXT("Living crew count is 0"), Ship->GetLivingCrewCount(), 0);
	TestTrue(TEXT("Anchor control allowed after crew eliminated"), Ship->AllowsPlayerAnchorControl());

	// Interaction now succeeds
	Ship->HandleAnchorInteracted(nullptr);
	TestTrue(TEXT("Anchor drops after crew eliminated"), Ship->IsAnchorDropped());

	return true;
}

#endif
