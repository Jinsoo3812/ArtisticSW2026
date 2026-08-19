#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ShipAI/EnemyShipNavigationTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipNavigationStateModelTest,
	"ArtisticSW.Enemy.Ship.Navigation.StateModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipNavigationStateModelTest::RunTest(const FString& Parameters)
{
	FEnemyShipNavigationProfile Profile;
	Profile.DetectionDistance = 10000.0f;
	Profile.IdealDistance = 2000.0f;
	Profile.OrbitTolerance = 500.0f;
	Profile.DangerCloseDistance = 1000.0f;
	Profile.ReturnArrivalDistance = 800.0f;

	FEnemyShipNavigationContext Context;
	Context.ShipForward = FVector::ForwardVector;
	Context.ShipRight = FVector::RightVector;

	FEnemyShipNavigationOutput Output = FEnemyShipNavigationModel::Evaluate(
		ENavalCombatState::Idle, Profile, Context);
	TestEqual(TEXT("No target and no home remains Idle"), Output.State, ENavalCombatState::Idle);
	TestEqual(TEXT("Idle produces no propulsion"), Output.MoveInput, 0.0f);

	Context.bHasTarget = true;
	Context.TargetLocation = FVector(5000.0f, 0.0f, 0.0f);
	Output = FEnemyShipNavigationModel::Evaluate(ENavalCombatState::Idle, Profile, Context);
	TestEqual(TEXT("Detected target enters Approach"), Output.State, ENavalCombatState::Approach);
	TestTrue(TEXT("Approach produces forward input"), Output.MoveInput > 0.99f);

	Context.TargetLocation = FVector(1800.0f, 0.0f, 0.0f);
	Output = FEnemyShipNavigationModel::Evaluate(ENavalCombatState::Approach, Profile, Context);
	TestEqual(TEXT("Ideal range enters Orbit"), Output.State, ENavalCombatState::Orbit);

	Context.TargetLocation = FVector(900.0f, 0.0f, 0.0f);
	Output = FEnemyShipNavigationModel::Evaluate(ENavalCombatState::Orbit, Profile, Context);
	TestEqual(TEXT("Danger-close range enters Retreat"), Output.State, ENavalCombatState::Retreat);

	Context.bHasTarget = false;
	Context.bHasHome = true;
	Context.HomeLocation = FVector(3000.0f, 0.0f, 0.0f);
	Output = FEnemyShipNavigationModel::Evaluate(ENavalCombatState::Approach, Profile, Context);
	TestEqual(TEXT("Lost target returns home"), Output.State, ENavalCombatState::Return);

	Context.ShipLocation = FVector(2500.0f, 0.0f, 0.0f);
	Output = FEnemyShipNavigationModel::Evaluate(ENavalCombatState::Return, Profile, Context);
	TestEqual(TEXT("Arrival radius returns to Idle"), Output.State, ENavalCombatState::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipNavigationOrbitDirectionTest,
	"ArtisticSW.Enemy.Ship.Navigation.OrbitDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipNavigationOrbitDirectionTest::RunTest(const FString& Parameters)
{
	FEnemyShipNavigationProfile Profile;
	Profile.IdealDistance = 2000.0f;
	FEnemyShipNavigationContext Context;
	Context.ShipForward = FVector::ForwardVector;
	Context.ShipRight = FVector::RightVector;
	Context.bHasTarget = true;
	Context.TargetLocation = FVector(2000.0f, 0.0f, 0.0f);

	Profile.bOrbitClockwise = true;
	const FEnemyShipNavigationOutput Clockwise = FEnemyShipNavigationModel::Evaluate(
		ENavalCombatState::Orbit, Profile, Context);
	Profile.bOrbitClockwise = false;
	const FEnemyShipNavigationOutput CounterClockwise = FEnemyShipNavigationModel::Evaluate(
		ENavalCombatState::Orbit, Profile, Context);

	TestTrue(TEXT("Orbit directions produce opposite lateral headings"), Clockwise.DesiredHeading.Y * CounterClockwise.DesiredHeading.Y < 0.0f);
	TestTrue(TEXT("Orbit directions produce opposite steering"), Clockwise.TurnInput * CounterClockwise.TurnInput < 0.0f);
	return true;
}

#endif
