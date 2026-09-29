#include "Room/SWRoomSaveStore.h"

#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomStateAdapter.h"
#include "Network/SWNetworkLog.h"
#include "SWRoomName.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
constexpr int64 MaxRoomFileBytes = 64ll * 1024 * 1024;
constexpr int32 MaxActorRecords = 100000;
constexpr int32 MaxRecordBytes = 1024 * 1024;
constexpr int32 MaxGuests = 256;
constexpr int32 MaxPlayerSlots = 40000;
constexpr int32 MaxStorageSlots = 40000;
constexpr uint32 RoomMagic = 0x33525753;
constexpr int32 HeaderBytes = sizeof(uint32) + sizeof(uint64) + sizeof(uint32);
FString SidecarPath(const TCHAR* Name)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), Name);
}
}

FString FSWRoomSaveStore::RoomPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), TEXT("CurrentRoom.v4.sav"));
}

bool FSWRoomSaveStore::Validate(const USWRoomSaveGame* Room)
{
	if (!ValidateHeader(Room) || !Room->bComplete || Room->MapPath.IsNull()
		|| Room->CaptureSequence == 0 || Room->WorldSnapshot.CaptureSequence != Room->CaptureSequence
		|| Room->WorldSnapshot.MapPath != Room->MapPath
		|| Room->WorldSnapshot.Actors.Num() > MaxActorRecords
		|| Room->WorldSnapshot.UnloadedActors.Num() > MaxActorRecords
		|| Room->WorldSnapshot.Actors.Num() > MaxActorRecords - Room->WorldSnapshot.UnloadedActors.Num()) return false;
	TSet<FGuid> ActorIds;
	auto ValidMotion = [](const FSWRoomMotionState& Motion)
	{
		return !Motion.LinearVelocity.ContainsNaN() && !Motion.AngularVelocityDegrees.ContainsNaN()
			&& !(Motion.bWasSimulatingPhysics && Motion.bWasProjectileMovementActive)
			&& (Motion.bHasMotion || (!Motion.bWasSimulatingPhysics && !Motion.bWasProjectileMovementActive));
	};
	TSet<FString> LoadedPackages;
	TSet<FString> UnloadedPackages;
	auto ValidAdapter = [](const FSWRoomActorRecord& Record)
	{
		if (Record.AdapterBytes.IsEmpty()) return Record.AdapterType.IsNone() && Record.AdapterVersion == 0;
		if (Record.AdapterType != TEXT("Domains") || Record.AdapterVersion != 1) return false;
		FSWRoomDomainPayload Payload;
		if (!FSWRoomStructCodec::Read(Record.AdapterBytes, Payload) || Payload.Parts.IsEmpty()) return false;
		uint8 PreviousDomain = 0;
		for (const FSWRoomDomainPart& Part : Payload.Parts)
		{
			const uint8 Domain = static_cast<uint8>(Part.Domain);
			if (Domain <= PreviousDomain || Domain > static_cast<uint8>(ESWRoomDomain::Boss)
				|| Part.Version <= 0 || Part.Bytes.IsEmpty() || Part.Bytes.Num() > MaxRecordBytes) return false;
			PreviousDomain = Domain;
		}
		return true;
	};
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.Actors)
	{
		if (!Record.StableId.IsValid() || Record.ClassPath.IsNull() || Record.LevelPartition.PackagePath.IsNull()
			|| Record.LevelPartition.InstanceName.IsNone()
			|| Record.WorldTransform.ContainsNaN()
			|| !ValidMotion(Record.MotionState)
			|| (Record.Origin == ESWRoomSpawnOrigin::Runtime && (!Record.CreatorId.IsValid() || Record.CreatorSequence == 0))
			|| Record.ContractVersion <= 0
			|| Record.SaveGameBytes.Num() > MaxRecordBytes || Record.AdapterBytes.Num() > MaxRecordBytes
			|| !ValidAdapter(Record)
			|| ActorIds.Contains(Record.StableId)) return false;
		ActorIds.Add(Record.StableId);
		LoadedPackages.Add(Record.LevelPartition.PackagePath.ToString() + TEXT("|") + Record.LevelPartition.InstanceName.ToString());
		TSet<FName> ComponentKeys;
		for (const FSWRoomComponentRecord& Component : Record.Components)
		{
			if (Component.StableKey.IsNone() || Component.SaveGameBytes.Num() > MaxRecordBytes
				|| Component.WorldTransform.ContainsNaN() || !ValidMotion(Component.MotionState)
				|| ComponentKeys.Contains(Component.StableKey)) return false;
			ComponentKeys.Add(Component.StableKey);
		}
	}
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.UnloadedActors)
	{
		if (!Record.StableId.IsValid() || Record.ClassPath.IsNull() || Record.LevelPartition.PackagePath.IsNull()
			|| Record.LevelPartition.InstanceName.IsNone()
			|| Record.WorldTransform.ContainsNaN()
			|| !ValidMotion(Record.MotionState)
			|| ActorIds.Contains(Record.StableId)
			|| (Record.Origin == ESWRoomSpawnOrigin::Runtime && (!Record.CreatorId.IsValid() || Record.CreatorSequence == 0))
			|| Record.ContractVersion <= 0
			|| Record.SaveGameBytes.Num() > MaxRecordBytes || Record.AdapterBytes.Num() > MaxRecordBytes
			|| !ValidAdapter(Record)) return false;
		ActorIds.Add(Record.StableId);
		UnloadedPackages.Add(Record.LevelPartition.PackagePath.ToString() + TEXT("|") + Record.LevelPartition.InstanceName.ToString());
		TSet<FName> ComponentKeys;
		for (const FSWRoomComponentRecord& Component : Record.Components)
		{
			if (Component.StableKey.IsNone() || Component.SaveGameBytes.Num() > MaxRecordBytes
				|| Component.WorldTransform.ContainsNaN() || !ValidMotion(Component.MotionState)
				|| ComponentKeys.Contains(Component.StableKey)) return false;
			ComponentKeys.Add(Component.StableKey);
		}
	}
	for (const FString& Package : LoadedPackages)
		if (UnloadedPackages.Contains(Package)) return false;
	TSet<FGuid> Tombstones;
	for (const FGuid& Id : Room->WorldSnapshot.DestroyedLevelActorIds)
	{
		if (!Id.IsValid() || ActorIds.Contains(Id) || Tombstones.Contains(Id)) return false;
		Tombstones.Add(Id);
	}
	TSet<FGuid> PartitionTombstones;
	for (const FSWRoomDestroyedActorPartition& Partition : Room->WorldSnapshot.DestroyedActorPartitions)
	{
		if (!Tombstones.Contains(Partition.StableId) || Partition.PackagePath.IsNull() || Partition.InstanceName.IsNone()
			|| PartitionTombstones.Contains(Partition.StableId)) return false;
		PartitionTombstones.Add(Partition.StableId);
	}
	TSet<FName> SystemKeys;
	for (const FSWRoomSystemRecord& System : Room->WorldSnapshot.Systems)
	{
		if (System.StableKey.IsNone() || System.ContractVersion <= 0 || System.SaveGameBytes.Num() > MaxRecordBytes
			|| SystemKeys.Contains(System.StableKey)) return false;
		SystemKeys.Add(System.StableKey);
	}
	for (const FGuid& Id : Room->WorldSnapshot.ReferenceIds) if (!ActorIds.Contains(Id)) return false;
	TSet<FString> IssueKeys;
	auto ValidateIssues = [&IssueKeys](const TArray<FSWRoomCaptureIssue>& Issues)
	{
		if (Issues.Num() > MaxActorRecords) return false;
		for (const FSWRoomCaptureIssue& Issue : Issues)
		{
			if (Issue.Domain.IsNone() || Issue.FieldKey.IsNone() || Issue.Reason.IsEmpty()) return false;
			if (Issue.Scope == ESWRoomIssueScope::WorldActor
				&& ((!Issue.StableId.IsValid() && Issue.OwnerPath.IsEmpty())
					|| (Issue.ClassPath.IsNull() && Issue.Domain != TEXT("Partition")))) return false;
			if (Issue.Scope == ESWRoomIssueScope::Player && Issue.PlayerKey.IsEmpty()) return false;
			const FString Owner = Issue.StableId.IsValid() ? Issue.StableId.ToString()
				: !Issue.PlayerKey.IsEmpty() ? Issue.PlayerKey : Issue.OwnerPath;
			const FString Key = FString::Printf(TEXT("%d|%s|%s|%s"), static_cast<int32>(Issue.Scope),
				*Owner, *Issue.Domain.ToString(), *Issue.FieldKey.ToString());
			if (IssueKeys.Contains(Key)) return false;
			IssueKeys.Add(Key);
		}
		return true;
	};
	if (!ValidateIssues(Room->WorldSnapshot.CaptureIssues)
		|| !ValidateIssues(Room->HostProgress.CaptureIssues)
		|| !ValidateIssues(Room->SharedProgress.CaptureIssues)) return false;
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
		if (!ValidateIssues(Guest.Progress.CaptureIssues)) return false;
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.Actors)
		for (const FGuid& Id : Record.ReferenceIds) if (!ActorIds.Contains(Id)) return false;
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.UnloadedActors)
		for (const FGuid& Id : Record.ReferenceIds) if (!ActorIds.Contains(Id)) return false;
	auto ValidPlayer = [](const FSWRoomPlayerProgress& Progress)
	{
		if (Progress.InventorySlots.Num() > MaxPlayerSlots || Progress.QuickSlotItemTags.Num() > 5
			|| Progress.UpgradeNodeIds.Num() > 4096 || Progress.Skills.Num() > 4096) return false;
		if (!FMath::IsFinite(Progress.CurrentHealth) || !FMath::IsFinite(Progress.MaximumHealth)
			|| !FMath::IsFinite(Progress.BaseStrength) || !FMath::IsFinite(Progress.BaseMoveSpeed)
			|| !FMath::IsFinite(Progress.BaseMoveSpeedMultiplier)
			|| !FMath::IsFinite(Progress.BaseAttackSpeedMultiplier)
			|| Progress.MaximumHealth < 0.f || Progress.BaseMoveSpeed < 0.f
			|| Progress.BaseMoveSpeedMultiplier < 0.f || Progress.BaseAttackSpeedMultiplier < 0.f) return false;
		for (const FSWInventorySlotSnapshot& Slot : Progress.InventorySlots)
			if (Slot.Tab > 3 || Slot.SlotIndex < 0 || Slot.SlotIndex >= 10000 || Slot.Count < 0) return false;
		return true;
	};
	if (!ValidPlayer(Room->HostProgress)) return false;
	FString Normalized;
	TSet<FString> GuestNames;
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
	{
		if (!FSWRoomName::Normalize(Guest.DisplayName, Normalized) || Normalized != Guest.DisplayName
			|| Guest.DisplayName == Room->HostDisplayName || GuestNames.Contains(Guest.DisplayName)
			|| !ValidPlayer(Guest.Progress)) return false;
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

bool FSWRoomSaveStore::ValidateHeader(const USWRoomSaveGame* Room)
{
	if (!Room || Room->SaveVersion != USWRoomSaveGame::CurrentVersion || !Room->RoomId.IsValid()
		|| Room->Guests.Num() > MaxGuests
		|| Room->ContentContractVersion != USWRoomSaveGame::CurrentContentContractVersion) return false;
	FString Normalized;
	if (!FSWRoomName::Normalize(Room->HostDisplayName, Normalized) || Normalized != Room->HostDisplayName) return false;
	return true;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadPath(UObject* Outer, const FString& Path, bool bHeaderOnly)
{
	TArray<uint8> Bytes;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const int64 Size = Files.FileSize(*Path);
	if (Size <= 0 || Size > MaxRoomFileBytes || !FFileHelper::LoadFileToArray(Bytes, *Path)) return nullptr;
	if (Bytes.Num() < HeaderBytes) return nullptr;
	uint32 Magic = 0;
	uint64 PayloadSize = 0;
	uint32 Checksum = 0;
	FMemory::Memcpy(&Magic, Bytes.GetData(), sizeof(Magic));
	FMemory::Memcpy(&PayloadSize, Bytes.GetData() + sizeof(Magic), sizeof(PayloadSize));
	FMemory::Memcpy(&Checksum, Bytes.GetData() + sizeof(Magic) + sizeof(PayloadSize), sizeof(Checksum));
	if (Magic != RoomMagic || PayloadSize != static_cast<uint64>(Bytes.Num() - HeaderBytes)
		|| FCrc::MemCrc32(Bytes.GetData() + HeaderBytes, static_cast<int32>(PayloadSize)) != Checksum) return nullptr;
	TArray<uint8> Payload;
	Payload.Append(Bytes.GetData() + HeaderBytes, static_cast<int32>(PayloadSize));
	USWRoomSaveGame* Room = Cast<USWRoomSaveGame>(UGameplayStatics::LoadGameFromMemory(Payload));
	if (!(bHeaderOnly ? ValidateHeader(Room) : Validate(Room))) return nullptr;
	if (Outer && Room) Room->Rename(nullptr, Outer);
	return Room;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadCurrentRoom(UObject* Outer)
{
	const FString Path = RoomPath();
	if (USWRoomSaveGame* Room = LoadPath(Outer, Path)) return Room;
	USWRoomSaveGame* Backup = LoadPath(Outer, SidecarPath(TEXT("CurrentRoom.v4.bak")));
	if (Backup)
	{
		Backup->bRecoveredFromBackup = true;
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=Load RoomId=%s Result=BackupRecovered Reason=CurrentMissingOrInvalid Sequence=%llu"),
			*Backup->RoomId.ToString(), Backup->CaptureSequence);
	}
	return Backup;
}

bool FSWRoomSaveStore::HasValidCurrentRoom()
{
	return LoadCurrentRoom(GetTransientPackage()) != nullptr;
}

bool FSWRoomSaveStore::HasLegacyRoomFile()
{
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	return !Files.FileExists(*RoomPath()) && Files.FileExists(*SidecarPath(TEXT("CurrentRoom.sav")));
}

bool FSWRoomSaveStore::WriteVerified(const USWRoomSaveGame* Room, const FString& Path, bool bHeaderOnly)
{
	if (!(bHeaderOnly ? ValidateHeader(Room) : Validate(Room))) return false;
	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(const_cast<USWRoomSaveGame*>(Room), Bytes)
		|| Bytes.IsEmpty() || Bytes.Num() > MaxRoomFileBytes - HeaderBytes) return false;
	const uint64 PayloadSize = Bytes.Num();
	const uint32 Checksum = FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
	TArray<uint8> FileBytes;
	FileBytes.SetNumUninitialized(HeaderBytes + Bytes.Num());
	FMemory::Memcpy(FileBytes.GetData(), &RoomMagic, sizeof(RoomMagic));
	FMemory::Memcpy(FileBytes.GetData() + sizeof(RoomMagic), &PayloadSize, sizeof(PayloadSize));
	FMemory::Memcpy(FileBytes.GetData() + sizeof(RoomMagic) + sizeof(PayloadSize), &Checksum, sizeof(Checksum));
	FMemory::Memcpy(FileBytes.GetData() + HeaderBytes, Bytes.GetData(), Bytes.Num());
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)) || !FFileHelper::SaveArrayToFile(FileBytes, *Path)) return false;
	USWRoomSaveGame* Checked = LoadPath(GetTransientPackage(), Path, bHeaderOnly);
	return Checked && Checked->RoomId == Room->RoomId && Checked->SaveVersion == Room->SaveVersion
		&& Checked->CaptureSequence == Room->CaptureSequence && Checked->MapPath == Room->MapPath;
}

bool FSWRoomSaveStore::WriteCurrentRoom(const USWRoomSaveGame* Room)
{
	return WriteCurrentRoomInternal(Room, false);
}

bool FSWRoomSaveStore::WriteCurrentRoomInternal(const USWRoomSaveGame* Room, bool bAllowInvalidCurrent)
{
	const FString Path = RoomPath();
	const FString Temp = SidecarPath(TEXT("CurrentRoom.v4.tmp"));
	const FString Backup = SidecarPath(TEXT("CurrentRoom.v4.bak"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const bool bHasInvalidCurrent = Files.FileExists(*Path) && !LoadPath(GetTransientPackage(), Path);
	if (bHasInvalidCurrent && !bAllowInvalidCurrent)
	{
		const USWRoomSaveGame* Recovery = LoadPath(GetTransientPackage(), Backup);
		if (!Recovery || Recovery->RoomId != Room->RoomId) return false;
	}
	if (bHasInvalidCurrent)
	{
		const FString CorruptPath = SidecarPath(*FString::Printf(TEXT("CurrentRoom.v4.corrupt.%lld.sav"), FDateTime::UtcNow().ToUnixTimestamp()));
		if (!Files.CopyFile(*CorruptPath, *Path)) return false;
	}
	Files.DeleteFile(*Temp);
	if (!WriteVerified(Room, Temp)) { Files.DeleteFile(*Temp); return false; }
	const bool bHadValidCurrent = LoadPath(GetTransientPackage(), Path) != nullptr;
	const bool bHadValidBackup = LoadPath(GetTransientPackage(), Backup) != nullptr;
	if (bHadValidCurrent)
	{
		Files.DeleteFile(*Backup);
		if (!Files.CopyFile(*Backup, *Path) || !LoadPath(GetTransientPackage(), Backup))
		{ Files.DeleteFile(*Temp); return false; }
	}
	bool bReplaced = false;
#if PLATFORM_WINDOWS
	bReplaced = ::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	bReplaced = Files.MoveFile(*Path, *Temp);
#endif
	if (bReplaced)
	{
		USWRoomSaveGame* Checked = LoadPath(GetTransientPackage(), Path);
		if (Checked && Checked->RoomId == Room->RoomId && Checked->CaptureSequence == Room->CaptureSequence) return true;
		if (bHadValidCurrent || bHadValidBackup)
		{
#if PLATFORM_WINDOWS
			::CopyFileW(*Backup, *Path, false);
#else
			Files.CopyFile(*Path, *Backup);
#endif
		}
		return false;
	}
	Files.DeleteFile(*Temp);
	return false;
}

bool FSWRoomSaveStore::StageNewRoom(const USWRoomSaveGame* Room)
{
	const FString Path = SidecarPath(TEXT("CurrentRoom.v4.pending.sav"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*Path);
	return Room && !Room->bComplete && WriteVerified(Room, Path, true);
}

bool FSWRoomSaveStore::StageCompleteNewRoom(const USWRoomSaveGame* Room)
{
	return WriteVerified(Room, SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
}

USWRoomSaveGame* FSWRoomSaveStore::LoadStagedNewRoom(UObject* Outer)
{
	return LoadPath(Outer, SidecarPath(TEXT("CurrentRoom.v4.pending.sav")), true);
}

bool FSWRoomSaveStore::CommitStagedNewRoom()
{
	USWRoomSaveGame* Pending = LoadPath(GetTransientPackage(), SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
	if (!Pending || Pending->SaveKind != ESWRoomSaveKind::New || !WriteCurrentRoomInternal(Pending, true)) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
	return true;
}

void FSWRoomSaveStore::DiscardStagedNewRoom()
{
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
}
