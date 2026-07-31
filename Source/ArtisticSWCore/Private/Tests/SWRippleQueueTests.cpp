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

#endif
