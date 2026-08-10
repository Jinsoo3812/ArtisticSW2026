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
#include "BaseGameplayTags.h"
#include "ShipAI/NavalAIController.h"

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

	UEnemyShipPatternData* Pattern = NewObject<UEnemyShipPatternData>();
	FEnemyShipSkillRule& RepeatRule = Pattern->SkillRules.AddDefaulted_GetRef();
	RepeatRule.AbilityTag = GameplayAbility_BasicAttack;
	RepeatRule.MinimumInterval = 5.0f;
	RepeatRule.MaximumDistance = 2000.0f;
	RepeatRule.Priority = 10;

	UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent();
	Runtime->SetPattern(Pattern);
	FEnemyShipAbilitySelection Selection;
	TestTrue(TEXT("Rule is initially eligible"), Runtime->SelectAbilityAtTime(Target, 0.0, Selection));
	TestTrue(TEXT("Initial selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("Rule is blocked before interval"), Runtime->SelectAbilityAtTime(Target, 4.99, Selection));
	TestTrue(TEXT("Rule is eligible at interval boundary"), Runtime->SelectAbilityAtTime(Target, 5.0, Selection));

	Pattern->SkillRules[0].bUseOnlyOnce = true;
	Runtime->ResetRuntimeState(0);
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
	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	Archetype->SpecRow.DataTable = SpecTable;
	Archetype->SpecRow.RowName = TEXT("SpecC");
	Archetype->Pattern = Pattern;
	Archetype->AbilitySet = AbilitySet;

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

#endif
