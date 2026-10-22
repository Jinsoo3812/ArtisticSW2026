#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Room/SWRoomProgressSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomWorldRecoveryPolicyTest,
	"ArtisticSW.Room.WorldRecovery.PreservesProgressAndLimitsRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomWorldRecoveryPolicyTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	USWRoomProgressSubsystem* Room = NewObject<USWRoomProgressSubsystem>(Instance);
	Room->bHostedRoom = true;
	Room->ActiveRoom = NewObject<USWRoomSaveGame>(Room);
	USWRoomSaveGame* Save = Room->ActiveRoom;
	Save->RoomId = FGuid::NewGuid();
	const FGuid RoomId = Save->RoomId;
	Save->CaptureSequence = 17;
	Save->HostProgress.UpgradeNodeIds.Add(TEXT("KeepUpgrade"));
	Save->HostProgress.MaximumHealth = 100.f;
	Save->HostProgress.CurrentHealth = 0.f;
	Save->HostProgress.bWasDead = true;
	Save->HostProgress.bHasResumeTransform = true;
	Save->HostProgress.bWasMounted = true;
	Save->HostProgress.MountedDeviceId = FGuid::NewGuid();
	Save->HostProgress.ShipStableId = FGuid::NewGuid();
	Save->HostProgress.bHasMovement = true;
	Save->HostProgress.WorldVelocity = FVector(100.f, 0.f, 0.f);
	Save->Guests.AddDefaulted_GetRef().Progress = Save->HostProgress;
	Save->SharedProgress.AppliedActionKeys.Add(TEXT("KeepStoryAction"));
	Save->SharedProgress.ShipUpgradeNodeIds.Add(TEXT("KeepShipUpgrade"));
	Save->WorldSnapshot.Actors.AddDefaulted_GetRef().StableId = FGuid::NewGuid();
	TestTrue(TEXT("Initial recovery accepted"), Room->BeginWorldRecovery());
	TestTrue(TEXT("Fresh-world recovery pending"), Room->IsWorldRecoveryPending());
	TestEqual(TEXT("Room identity preserved"), Save->RoomId, RoomId);
	TestEqual(TEXT("Committed sequence not advanced"), Save->CaptureSequence, uint64(17));
	TestEqual(TEXT("Player upgrade preserved"), Save->HostProgress.UpgradeNodeIds.Num(), 1);
	TestEqual(TEXT("Story completion preserved"), Save->SharedProgress.AppliedActionKeys.Num(), 1);
	TestEqual(TEXT("Ship upgrade preserved"), Save->SharedProgress.ShipUpgradeNodeIds.Num(), 1);
	TestEqual(TEXT("Original snapshot retained until next save"), Save->WorldSnapshot.Actors.Num(), 1);
	for (const FSWRoomPlayerProgress* Progress : { &Save->HostProgress, &Save->Guests[0].Progress })
	{
		TestFalse(TEXT("Old-world transform disabled"), Progress->bHasResumeTransform);
		TestFalse(TEXT("Missing mount reference removed"), Progress->MountedDeviceId.IsValid());
		TestFalse(TEXT("Old ship reference removed"), Progress->ShipStableId.IsValid());
		TestFalse(TEXT("Old motion disabled"), Progress->bHasMovement);
		TestFalse(TEXT("Fresh entry is alive"), Progress->bWasDead);
		TestEqual(TEXT("Fresh entry health"), Progress->CurrentHealth, 100.f);
	}
	Room->CompleteWorldRecovery();
	TestFalse(TEXT("Completion releases recovery barrier"), Room->IsWorldRecoveryPending());
	TestTrue(TEXT("Recovery notice remains available"), Room->WasWorldRecovered());
	TestFalse(TEXT("Repeated failure cannot loop"), Room->BeginWorldRecovery());
	USWRoomProgressSubsystem* NewRoom = NewObject<USWRoomProgressSubsystem>(Instance);
	NewRoom->bHostedRoom = true;
	NewRoom->ActiveRoom = NewObject<USWRoomSaveGame>(NewRoom);
	NewRoom->bNewRoomPending = true;
	TestFalse(TEXT("New-room setup cannot masquerade as recovery"), NewRoom->BeginWorldRecovery());
	return true;
}
#endif
