#pragma once

#include "CoreMinimal.h"

class USWRoomSaveGame;

class ARTISTICSWCORE_API FSWRoomSaveStore
{
public:
	static USWRoomSaveGame* LoadCurrentRoom(UObject* Outer);
	static bool WriteCurrentRoom(const USWRoomSaveGame* Room);
	static bool HasValidCurrentRoom();
	static bool HasLegacyRoomFile();
	static bool StageNewRoom(const USWRoomSaveGame* Room);
	static bool StageCompleteNewRoom(const USWRoomSaveGame* Room);
	static USWRoomSaveGame* LoadStagedNewRoom(UObject* Outer);
	static bool CommitStagedNewRoom();
	static void DiscardStagedNewRoom();
	static bool Validate(const USWRoomSaveGame* Room);
	static bool ValidateHeader(const USWRoomSaveGame* Room);
private:
	static FString RoomPath();
	static USWRoomSaveGame* LoadPath(UObject* Outer, const FString& Path, bool bHeaderOnly = false);
	static bool WriteVerified(const USWRoomSaveGame* Room, const FString& Path, bool bHeaderOnly = false);
	static bool WriteCurrentRoomInternal(const USWRoomSaveGame* Room, bool bAllowInvalidCurrent);
};
