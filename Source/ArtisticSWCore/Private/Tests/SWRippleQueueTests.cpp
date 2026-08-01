#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Water/SWRippleSettings.h"
#include "Water/SWRippleTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWRippleQueueDeterminismTest,
	"ArtisticSW.Water.RippleQueueDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWRippleQueueDeterminismTest::RunTest(const FString& Parameters)
{
	const int32 Capacity = GetDefault<USWRippleSettings>()->GetMaxRippleCount();
	TArray<FSWRippleEvent> ServerEvents;
	TArray<FSWRippleEvent> ClientEvents;

	for (int32 EventId = 1; EventId <= Capacity + 5; ++EventId)
	{
		FSWRippleEvent Event;
		Event.EventId = EventId;
		Event.StartServerTime = EventId;
		Event.ExpireServerTime = EventId + 10.0;
		Event.InitialAmplitude = 1.0f;

		FSWRippleQueuePolicy::AddOrUpdateCapped(ServerEvents, Event, Capacity);
		FSWRippleQueuePolicy::AddOrUpdateCapped(ClientEvents, Event, Capacity);
	}

	TestEqual(TEXT("Server queue uses configured capacity"), ServerEvents.Num(), Capacity);
	TestEqual(TEXT("Client queue uses configured capacity"), ClientEvents.Num(), Capacity);

	ServerEvents.Sort([](const FSWRippleEvent& A, const FSWRippleEvent& B)
	{
		return A.EventId < B.EventId;
	});
	ClientEvents.Sort([](const FSWRippleEvent& A, const FSWRippleEvent& B)
	{
		return A.EventId < B.EventId;
	});

	for (int32 Index = 0; Index < Capacity; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Server and client retain the same event at index %d"), Index),
			ServerEvents[Index].EventId,
			ClientEvents[Index].EventId);
	}
	TestEqual(TEXT("FIFO eviction removes the five oldest events"), ServerEvents[0].EventId, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWRippleClientRenderClockTest,
	"ArtisticSW.Water.RippleClientRenderClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWRippleClientRenderClockTest::RunTest(const FString& Parameters)
{
	FSWRippleClientRenderClock Clock;
	TestEqual(TEXT("Unanchored clock uses the GameState estimate"), Clock.Resolve(41.3, 10.0), 41.3);

	Clock.ObserveReplicatedEvent(42.0, 41.3, 10.0);
	TestTrue(TEXT("A received event anchors a clock that is 700 ms behind"), Clock.IsAnchored());
	TestEqual(TEXT("Received event is active immediately"), Clock.Resolve(41.3, 10.0), 42.0);
	TestEqual(TEXT("Anchor advances with local world time"), Clock.Resolve(41.8, 10.25), 42.25);
	TestEqual(TEXT("A newer GameState estimate takes over without moving backward"), Clock.Resolve(42.6, 10.3), 42.6);

	Clock.ObserveReplicatedEvent(42.4, 42.6, 10.3);
	TestEqual(TEXT("Past events never rewind the render clock"), Clock.Resolve(42.6, 10.3), 42.6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWRipplePredictionMatchingTest,
	"ArtisticSW.Water.RipplePredictionMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWRipplePredictionMatchingTest::RunTest(const FString& Parameters)
{
	TArray<FSWRippleEvent> Events;
	FSWRippleEvent FarPrediction;
	FarPrediction.EventId = -1;
	FarPrediction.Origin = FVector2D(500.0f, 0.0f);
	FarPrediction.StartServerTime = 10.0;
	Events.Add(FarPrediction);

	FSWRippleEvent MatchingPrediction;
	MatchingPrediction.EventId = -2;
	MatchingPrediction.Origin = FVector2D(12.0f, -8.0f);
	MatchingPrediction.StartServerTime = 10.4;
	Events.Add(MatchingPrediction);

	FSWRippleEvent AuthoritativeEvent;
	AuthoritativeEvent.EventId = 7;
	AuthoritativeEvent.Origin = FVector2D(10.0f, -10.0f);
	AuthoritativeEvent.StartServerTime = 10.0;
	Events.Add(AuthoritativeEvent);

	TestEqual(
		TEXT("Closest spatial prediction is reconciled"),
		FSWRipplePredictionPolicy::FindBestPredictedEventIndex(Events, AuthoritativeEvent, 250.0f, 1.5),
		1);
	AuthoritativeEvent.StartServerTime = 20.0;
	TestEqual(
		TEXT("Old predictions are not reconciled"),
		FSWRipplePredictionPolicy::FindBestPredictedEventIndex(Events, AuthoritativeEvent, 250.0f, 1.5),
		INDEX_NONE);
	return true;
}

#endif
