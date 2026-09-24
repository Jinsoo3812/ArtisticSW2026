#include "Room/SWRoomProgressSubsystem.h"

#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSaveStore.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void USWRoomProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString RunText, Mode, RoomText, HostText;
	if (!IsRunningDedicatedServer() || !FParse::Value(FCommandLine::Get(), TEXT("SWRoomRunId="), RunText)) return;
	bHostedRoom = true;
	FGuid RunId, RoomId;
	if (!FGuid::Parse(RunText, RunId) || !RunId.IsValid()
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomMode="), Mode)
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomId="), RoomText)
		|| !FGuid::Parse(RoomText, RoomId) || !RoomId.IsValid()
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomHostKey="), HostText)
		|| !FGuid::Parse(HostText, HostKey) || !HostKey.IsValid())
	{
		bStartupError = true;
		return;
	}
	if (Mode == TEXT("Continue"))
	{
		ActiveRoom = FSWRoomSaveStore::LoadCurrentRoom(this);
		bStartupError = !ActiveRoom || ActiveRoom->RoomId != RoomId;
	}
	else if (Mode == TEXT("New"))
	{
		ActiveRoom = NewObject<USWRoomSaveGame>(this);
		ActiveRoom->RoomId = RoomId;
	}
	else bStartupError = true;
}

bool USWRoomProgressSubsystem::WriteCheckpoint()
{
	return bHostedRoom && !bStartupError && ActiveRoom && FSWRoomSaveStore::WriteCurrentRoom(ActiveRoom);
}
