#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "SWShipWakeSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWShipWakeAuthoritativeQueryTest,
	"ArtisticSW.Water.ShipWake.AuthoritativeQuerySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWShipWakeAuthoritativeQueryTest::RunTest(const FString& Parameters)
{
	UWorld* World = NewObject<UWorld>();
	USWShipWakeSubsystem* Wake = NewObject<USWShipWakeSubsystem>(World);
	FSWShipWakeEvent Event;
	Event.EventId = 42;
	Event.StartServerTime = 10.0;
	Event.EndServerTime = 11.0;
	Event.ExpireServerTime = 30.0;
	Event.InitialAmplitudeCm = 50.0f;
	Wake->AddOrUpdateReplicatedEvent(Event);
	Wake->SubmitPredictedEvent(Event);
	TArray<FSWShipWakeEvent> Snapshot;
	Wake->GetActiveAuthoritativeEventsSnapshot(20.0, Snapshot);
	TestEqual(TEXT("Only the server event enters the water-query snapshot"), Snapshot.Num(), 1);
	if (Snapshot.Num() == 1) TestEqual(TEXT("Server event identity is retained"), Snapshot[0].EventId, 42);
	Wake->GetEventsSnapshot(Snapshot);
	TestEqual(TEXT("The shared snapshot still contains both events"), Snapshot.Num(), 2);
	Wake->GetActiveEventsSnapshot(20.0, Snapshot);
	TestEqual(TEXT("Existing active snapshot behavior is unchanged"), Snapshot.Num(), 2);
	Wake->GetActiveAuthoritativeEventsSnapshot(9.0, Snapshot);
	TestEqual(TEXT("Events before their start are excluded"), Snapshot.Num(), 0);
	Wake->GetActiveAuthoritativeEventsSnapshot(30.0, Snapshot);
	TestEqual(TEXT("Expired events are excluded"), Snapshot.Num(), 0);
	Wake->Events.RemoveAll([](const FSWShipWakeEvent& Item) { return Item.EventId >= 0; });
	TestEqual(TEXT("Prediction alone contributes no query height"), Wake->GetAuthoritativeWakeHeight(FVector::ZeroVector, 20.0), 0.0f);
	TestTrue(TEXT("Prediction alone contributes no query gradient"), Wake->GetAuthoritativeWakeGradient(FVector::ZeroVector, 20.0).IsNearlyZero());
	Event.EventId = 0;
	Wake->SubmitAuthoritativeEvent(Event);
	Wake->GetActiveAuthoritativeEventsSnapshot(20.0, Snapshot);
	TestEqual(TEXT("Pre-replicator authority fallback is preserved"), Snapshot.Num(), 1);
	return true;
}

#endif
