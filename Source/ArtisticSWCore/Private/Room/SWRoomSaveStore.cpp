#include "Room/SWRoomSaveStore.h"

#include "Room/SWRoomSaveGame.h"
#include "SWRoomName.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
constexpr int64 MaxRoomFileBytes = 4 * 1024 * 1024;
constexpr int32 MaxGuests = 256;
constexpr int32 MaxPlayerSlots = 40000;
constexpr int32 MaxStorageSlots = 40000;
FString SidecarPath(const TCHAR* Name)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), Name);
}
}

FString FSWRoomSaveStore::RoomPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), TEXT("CurrentRoom.sav"));
}

bool FSWRoomSaveStore::Validate(const USWRoomSaveGame* Room)
{
	if (!Room || Room->SaveVersion != 1 || !Room->RoomId.IsValid() || Room->Guests.Num() > MaxGuests) return false;
	FString Normalized;
	if (!FSWRoomName::Normalize(Room->HostDisplayName, Normalized) || Normalized != Room->HostDisplayName) return false;
	auto ValidPlayer = [](const FSWRoomPlayerProgress& Progress)
	{
		if (Progress.InventorySlots.Num() > MaxPlayerSlots || Progress.QuickSlotItemTags.Num() > 5
			|| Progress.UpgradeNodeIds.Num() > 4096 || Progress.Skills.Num() > 4096) return false;
		for (const FSWInventorySlotSnapshot& Slot : Progress.InventorySlots)
			if (Slot.Tab > 3 || Slot.SlotIndex < 0 || Slot.SlotIndex >= 10000 || Slot.Count < 0) return false;
		return true;
	};
	if (!ValidPlayer(Room->HostProgress)) return false;
	TSet<FString> GuestNames;
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
	{
		if (!FSWRoomName::Normalize(Guest.DisplayName, Normalized) || Normalized != Guest.DisplayName
			|| GuestNames.Contains(Guest.DisplayName) || !ValidPlayer(Guest.Progress)) return false;
		GuestNames.Add(Guest.DisplayName);
	}
	TSet<FString> StorageKeys;
	for (const FSWRoomStorageProgress& Storage : Room->SharedProgress.Storage)
	{
		if (!Storage.ChestId.IsValid() || Storage.SaveNamespace.Len() > 128 || Storage.SlotsPerTab < 1
			|| Storage.SlotsPerTab > 10000 || Storage.Slots.Num() != Storage.SlotsPerTab * 4
			|| Storage.Slots.Num() > MaxStorageSlots) return false;
		const FString Key = Storage.ChestId.ToString() + TEXT("|") + Storage.SaveNamespace;
		if (StorageKeys.Contains(Key)) return false;
		StorageKeys.Add(Key);
		for (const FSWRoomStorageSlot& Slot : Storage.Slots) if (Slot.Count < 0) return false;
	}
	return true;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadPath(UObject* Outer, const FString& Path)
{
	TArray<uint8> Bytes;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const int64 Size = Files.FileSize(*Path);
	if (Size <= 0 || Size > MaxRoomFileBytes || !FFileHelper::LoadFileToArray(Bytes, *Path)) return nullptr;
	USWRoomSaveGame* Room = Cast<USWRoomSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!Validate(Room)) return nullptr;
	if (Outer && Room) Room->Rename(nullptr, Outer);
	return Room;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadCurrentRoom(UObject* Outer)
{
	const FString Path = RoomPath();
	if (USWRoomSaveGame* Room = LoadPath(Outer, Path)) return Room;
	return LoadPath(Outer, SidecarPath(TEXT("CurrentRoom.bak")));
}

bool FSWRoomSaveStore::HasValidCurrentRoom()
{
	return LoadCurrentRoom(GetTransientPackage()) != nullptr;
}

bool FSWRoomSaveStore::WriteVerified(const USWRoomSaveGame* Room, const FString& Path)
{
	if (!Validate(Room)) return false;
	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(const_cast<USWRoomSaveGame*>(Room), Bytes)
		|| Bytes.IsEmpty() || Bytes.Num() > MaxRoomFileBytes) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)) || !FFileHelper::SaveArrayToFile(Bytes, *Path)) return false;
	USWRoomSaveGame* Checked = LoadPath(GetTransientPackage(), Path);
	return Checked && Checked->RoomId == Room->RoomId && Checked->SaveVersion == Room->SaveVersion;
}

bool FSWRoomSaveStore::WriteCurrentRoom(const USWRoomSaveGame* Room)
{
	const FString Path = RoomPath();
	const FString Temp = SidecarPath(TEXT("CurrentRoom.tmp"));
	const FString Backup = SidecarPath(TEXT("CurrentRoom.bak"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*Temp);
	if (!WriteVerified(Room, Temp)) { Files.DeleteFile(*Temp); return false; }
	const bool bHadValidCurrent = LoadPath(GetTransientPackage(), Path) != nullptr;
	if (bHadValidCurrent)
	{
		Files.DeleteFile(*Backup);
		if (!Files.MoveFile(*Backup, *Path)) { Files.DeleteFile(*Temp); return false; }
	}
	else if (Files.FileExists(*Path)) Files.DeleteFile(*Path);
	if (Files.MoveFile(*Path, *Temp)) return true;
	if (bHadValidCurrent) Files.MoveFile(*Path, *Backup);
	Files.DeleteFile(*Temp);
	return false;
}

bool FSWRoomSaveStore::StageNewRoom(const USWRoomSaveGame* Room)
{
	const FString Path = SidecarPath(TEXT("CurrentRoom.pending.sav"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*Path);
	return WriteVerified(Room, Path);
}

bool FSWRoomSaveStore::CommitStagedNewRoom()
{
	const FString Path = RoomPath();
	USWRoomSaveGame* Pending = LoadPath(GetTransientPackage(), SidecarPath(TEXT("CurrentRoom.pending.sav")));
	if (!Pending || !WriteCurrentRoom(Pending)) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const FString Backup = SidecarPath(TEXT("CurrentRoom.bak"));
	if (Files.FileExists(*Backup) && !Files.DeleteFile(*Backup))
	{
		Files.DeleteFile(*Path);
		Files.MoveFile(*Path, *Backup);
		return false;
	}
	Files.DeleteFile(*SidecarPath(TEXT("CurrentRoom.pending.sav")));
	return true;
}

void FSWRoomSaveStore::DiscardStagedNewRoom()
{
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*SidecarPath(TEXT("CurrentRoom.pending.sav")));
}
