#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Task/BTT_MoveToDeckWaypoint.h"
#include "Task/BTT_SelectDeckWaypoint.h"
#include "Task/BTT_WaitAtDeckWaypoint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckEnemyMVPDefaultsTest,
	"ArtisticSW.Enemy.DeckMVP.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckEnemyMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const UDeckWaypointComponent* WaypointCDO = GetDefault<UDeckWaypointComponent>();
	const ADeckRangedEnemy* EnemyCDO = GetDefault<ADeckRangedEnemy>();
	const UBTT_MoveToDeckWaypoint* MoveTaskCDO = GetDefault<UBTT_MoveToDeckWaypoint>();
	const UBTT_SelectDeckWaypoint* SelectTaskCDO = GetDefault<UBTT_SelectDeckWaypoint>();
	const UBTT_WaitAtDeckWaypoint* WaitTaskCDO = GetDefault<UBTT_WaitAtDeckWaypoint>();

	TestNotNull(TEXT("Deck waypoint component exists"), WaypointCDO);
	TestFalse(TEXT("Waypoint has no independent tick"), WaypointCDO->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("Waypoint has no independent replication"), WaypointCDO->GetIsReplicated());
	TestTrue(TEXT("Waypoint is patrol-enabled by default"), WaypointCDO->CanPatrol());
	TestNotNull(TEXT("Pooled deck enemy class exists"), EnemyCDO);
	TestFalse(TEXT("Deck enemy does not force global relevancy"), EnemyCDO->bAlwaysRelevant);
	TestTrue(TEXT("Deck enemy actor remains replicated while active"), EnemyCDO->GetIsReplicated());
	TestNotNull(TEXT("Live-goal move task exists"), MoveTaskCDO);
	TestNotNull(TEXT("Waypoint selection task exists"), SelectTaskCDO);
	TestNotNull(TEXT("Waypoint wait task exists"), WaitTaskCDO);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeckWaypointFollowsParentTransformTest,
	"ArtisticSW.Enemy.DeckMVP.WaypointFollowsMovingDeck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeckWaypointFollowsParentTransformTest::RunTest(const FString& Parameters)
{
	USceneComponent* Deck = NewObject<USceneComponent>();
	UDeckWaypointComponent* Waypoint = NewObject<UDeckWaypointComponent>();
	Waypoint->SetupAttachment(Deck);
	Waypoint->SetRelativeLocation(FVector(200.0f, 50.0f, 0.0f));

	Deck->SetWorldTransform(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(1000.0f, 2000.0f, 300.0f)));
	Deck->UpdateComponentToWorld();
	Waypoint->UpdateComponentToWorld();
	const FVector FirstExpected = Deck->GetComponentTransform().TransformPosition(Waypoint->GetRelativeLocation());
	TestTrue(TEXT("Waypoint derives its first world point from the deck"),
		Waypoint->GetComponentLocation().Equals(FirstExpected, 0.1f));

	Deck->SetWorldTransform(FTransform(FRotator(8.0f, 130.0f, -4.0f), FVector(1300.0f, 2400.0f, 380.0f)));
	Deck->UpdateComponentToWorld();
	Waypoint->UpdateComponentToWorld();
	const FVector MovedExpected = Deck->GetComponentTransform().TransformPosition(Waypoint->GetRelativeLocation());
	TestTrue(TEXT("Waypoint follows translation, yaw, pitch, and roll without its own tick"),
		Waypoint->GetComponentLocation().Equals(MovedExpected, 0.1f));
	return true;
}

#endif
