#pragma once

#include "CoreMinimal.h"

class USWRoomSaveGame;

class ARTISTICSWCORE_API FSWRoomSaveStore
{
public:
	static USWRoomSaveGame* LoadCurrentRoom(UObject* Outer);
	static bool WriteCurrentRoom(const USWRoomSaveGame* Room);
	static bool HasValidCurrentRoom();
	static bool StageNewRoom(const USWRoomSaveGame* Room);
	static bool CommitStagedNewRoom();
	static void DiscardStagedNewRoom();
	static bool Validate(const USWRoomSaveGame* Room);
private:
	static FString RoomPath();
	static USWRoomSaveGame* LoadPath(UObject* Outer, const FString& Path);
	static bool WriteVerified(const USWRoomSaveGame* Room, const FString& Path);
};
