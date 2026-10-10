#include "Room/SWRoomSaveStore.h"
#include "Room/SWRoomRuntimePaths.h"

#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomStateAdapter.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWRoomLoadDiagnostics.h"
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
	const FString Directory = FSWRoomRuntimePaths::GetSaveDirectory();
	return Directory.IsEmpty() ? FString() : FPaths::Combine(Directory, Name);
}
}

FString FSWRoomSaveStore::RoomPath()
{
	return SidecarPath(TEXT("CurrentRoom.v4.sav"));
}

bool FSWRoomSaveStore::Validate(const USWRoomSaveGame* Room)
{
	auto Fail = [Room](const TCHAR* Reason)
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveValidation Result=Failed RoomId=%s Sequence=%llu Reason=%s"),
			Room ? *Room->RoomId.ToString() : TEXT("None"), Room ? Room->CaptureSequence : 0, Reason);
		return false;
	};
	if (!ValidateHeader(Room) || !Room->bComplete || Room->MapPath.IsNull()
		|| Room->CaptureSequence == 0 || Room->WorldSnapshot.CaptureSequence != Room->CaptureSequence
		|| Room->WorldSnapshot.MapPath != Room->MapPath
		|| Room->WorldSnapshot.Actors.Num() > MaxActorRecords
		|| Room->WorldSnapshot.UnloadedActors.Num() > MaxActorRecords
		|| Room->WorldSnapshot.Actors.Num() > MaxActorRecords - Room->WorldSnapshot.UnloadedActors.Num()) return Fail(TEXT("HeaderCompletionMapSequenceOrActorCount"));
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
	auto ActorFailure = [&ValidMotion, &ValidAdapter, &ActorIds](const FSWRoomActorRecord& Record)
	{
		TArray<FString> Reasons;
		if (!Record.StableId.IsValid()) Reasons.Add(TEXT("InvalidStableId"));
		if (Record.ClassPath.IsNull()) Reasons.Add(TEXT("MissingClass"));
		if (Record.LevelPartition.PackagePath.IsNull()) Reasons.Add(TEXT("MissingPartitionPackage"));
		if (Record.LevelPartition.InstanceName.IsNone()) Reasons.Add(TEXT("MissingPartitionInstance"));
		if (Record.WorldTransform.ContainsNaN()) Reasons.Add(TEXT("NonFiniteTransform"));
		if (Record.AttachParentId == Record.StableId) Reasons.Add(TEXT("SelfAttachment"));
		if (Record.AttachParentId.IsValid() == Record.AttachParentComponentName.IsNone()) Reasons.Add(TEXT("AttachmentComponentMismatch"));
		if (!ValidMotion(Record.MotionState)) Reasons.Add(TEXT("InvalidMotion"));
		if (Record.Origin == ESWRoomSpawnOrigin::Runtime && (!Record.CreatorId.IsValid() || Record.CreatorSequence == 0)) Reasons.Add(TEXT("InvalidRuntimeCreator"));
		if (Record.ContractVersion <= 0) Reasons.Add(TEXT("InvalidContractVersion"));
		if (Record.SaveGameBytes.Num() > MaxRecordBytes) Reasons.Add(TEXT("ActorPayloadTooLarge"));
		if (Record.AdapterBytes.Num() > MaxRecordBytes) Reasons.Add(TEXT("AdapterPayloadTooLarge"));
		if (!ValidAdapter(Record)) Reasons.Add(TEXT("InvalidAdapterPayload"));
		if (ActorIds.Contains(Record.StableId)) Reasons.Add(TEXT("DuplicateActorId"));
		return FString::Join(Reasons, TEXT(","));
	};
	auto TraceActorFailure = [Room](const FSWRoomActorRecord& Record, const TCHAR* Scope, const FString& Reason)
	{
		UE_LOG(LogSWRoomSave, Error,
			TEXT("Flow=ActorValidation Result=Failed RoomId=%s Sequence=%llu Scope=%s Id=%s Class=%s Partition=%s Origin=%s CreatorId=%s CreatorSequence=%llu Contract=%d SaveGameBytes=%d AdapterBytes=%d ParentId=%s ParentComponent=%s Reason=%s"),
			*Room->RoomId.ToString(), Room->CaptureSequence, Scope, *Record.StableId.ToString(),
			*Record.ClassPath.ToString(), *Record.LevelPartition.PackagePath.ToString(),
			*UEnum::GetValueAsString(Record.Origin), *Record.CreatorId.ToString(), Record.CreatorSequence,
			Record.ContractVersion, Record.SaveGameBytes.Num(), Record.AdapterBytes.Num(),
			*Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString(), *Reason);
	};
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.Actors)
	{
		if (!Record.StableId.IsValid() || Record.ClassPath.IsNull() || Record.LevelPartition.PackagePath.IsNull()
			|| Record.LevelPartition.InstanceName.IsNone()
			|| Record.WorldTransform.ContainsNaN()
			|| Record.AttachParentId == Record.StableId
			|| (Record.AttachParentId.IsValid() == Record.AttachParentComponentName.IsNone())
			|| !ValidMotion(Record.MotionState)
			|| (Record.Origin == ESWRoomSpawnOrigin::Runtime && (!Record.CreatorId.IsValid() || Record.CreatorSequence == 0))
			|| Record.ContractVersion <= 0
			|| Record.SaveGameBytes.Num() > MaxRecordBytes || Record.AdapterBytes.Num() > MaxRecordBytes
			|| !ValidAdapter(Record)
			|| ActorIds.Contains(Record.StableId))
		{
			TraceActorFailure(Record, TEXT("Loaded"), ActorFailure(Record));
			return false;
		}
		ActorIds.Add(Record.StableId);
		LoadedPackages.Add(Record.LevelPartition.PackagePath.ToString() + TEXT("|") + Record.LevelPartition.InstanceName.ToString());
		TSet<FName> ComponentKeys;
		for (const FSWRoomComponentRecord& Component : Record.Components)
		{
			if (Component.StableKey.IsNone() || Component.SaveGameBytes.Num() > MaxRecordBytes
				|| Component.WorldTransform.ContainsNaN() || !ValidMotion(Component.MotionState)
				|| ComponentKeys.Contains(Component.StableKey))
			{
				UE_LOG(LogSWRoomSave, Error,
					TEXT("Flow=ComponentValidation Result=Failed RoomId=%s Sequence=%llu Scope=Loaded ActorId=%s Key=%s Bytes=%d InvalidKey=%d NonFiniteTransform=%d InvalidMotion=%d DuplicateKey=%d"),
					*Room->RoomId.ToString(), Room->CaptureSequence, *Record.StableId.ToString(),
					*Component.StableKey.ToString(), Component.SaveGameBytes.Num(), Component.StableKey.IsNone(),
					Component.WorldTransform.ContainsNaN(), !ValidMotion(Component.MotionState), ComponentKeys.Contains(Component.StableKey));
				return false;
			}
			ComponentKeys.Add(Component.StableKey);
		}
	}
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.UnloadedActors)
	{
		if (!Record.StableId.IsValid() || Record.ClassPath.IsNull() || Record.LevelPartition.PackagePath.IsNull()
			|| Record.LevelPartition.InstanceName.IsNone()
			|| Record.WorldTransform.ContainsNaN()
			|| Record.AttachParentId == Record.StableId
			|| (Record.AttachParentId.IsValid() == Record.AttachParentComponentName.IsNone())
			|| !ValidMotion(Record.MotionState)
			|| ActorIds.Contains(Record.StableId)
			|| (Record.Origin == ESWRoomSpawnOrigin::Runtime && (!Record.CreatorId.IsValid() || Record.CreatorSequence == 0))
			|| Record.ContractVersion <= 0
			|| Record.SaveGameBytes.Num() > MaxRecordBytes || Record.AdapterBytes.Num() > MaxRecordBytes
			|| !ValidAdapter(Record))
		{
			TraceActorFailure(Record, TEXT("Unloaded"), ActorFailure(Record));
			return false;
		}
		ActorIds.Add(Record.StableId);
		UnloadedPackages.Add(Record.LevelPartition.PackagePath.ToString() + TEXT("|") + Record.LevelPartition.InstanceName.ToString());
		TSet<FName> ComponentKeys;
		for (const FSWRoomComponentRecord& Component : Record.Components)
		{
			if (Component.StableKey.IsNone() || Component.SaveGameBytes.Num() > MaxRecordBytes
				|| Component.WorldTransform.ContainsNaN() || !ValidMotion(Component.MotionState)
				|| ComponentKeys.Contains(Component.StableKey))
			{
				UE_LOG(LogSWRoomSave, Error,
					TEXT("Flow=ComponentValidation Result=Failed RoomId=%s Sequence=%llu Scope=Unloaded ActorId=%s Key=%s Bytes=%d InvalidKey=%d NonFiniteTransform=%d InvalidMotion=%d DuplicateKey=%d"),
					*Room->RoomId.ToString(), Room->CaptureSequence, *Record.StableId.ToString(),
					*Component.StableKey.ToString(), Component.SaveGameBytes.Num(), Component.StableKey.IsNone(),
					Component.WorldTransform.ContainsNaN(), !ValidMotion(Component.MotionState), ComponentKeys.Contains(Component.StableKey));
				return false;
			}
			ComponentKeys.Add(Component.StableKey);
		}
	}
	for (const FString& Package : LoadedPackages)
		if (UnloadedPackages.Contains(Package))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=PartitionValidation Result=Failed Package=%s Reason=LoadedAndUnloaded"), *Package);
			return false;
		}
	TSet<FGuid> Tombstones;
	for (const FGuid& Id : Room->WorldSnapshot.DestroyedLevelActorIds)
	{
		if (!Id.IsValid() || ActorIds.Contains(Id) || Tombstones.Contains(Id))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=TombstoneValidation Result=Failed Id=%s Invalid=%d ConflictsWithActor=%d Duplicate=%d"),
				*Id.ToString(), !Id.IsValid(), ActorIds.Contains(Id), Tombstones.Contains(Id));
			return false;
		}
		Tombstones.Add(Id);
	}
	TSet<FGuid> PartitionTombstones;
	for (const FSWRoomDestroyedActorPartition& Partition : Room->WorldSnapshot.DestroyedActorPartitions)
	{
		if (!Tombstones.Contains(Partition.StableId) || Partition.PackagePath.IsNull() || Partition.InstanceName.IsNone()
			|| PartitionTombstones.Contains(Partition.StableId))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=TombstoneValidation Result=Failed Id=%s Package=%s Instance=%s Reason=InvalidPartitionOrDuplicate"),
				*Partition.StableId.ToString(), *Partition.PackagePath.ToString(), *Partition.InstanceName.ToString());
			return false;
		}
		PartitionTombstones.Add(Partition.StableId);
	}
	TSet<FName> SystemKeys;
	for (const FSWRoomSystemRecord& System : Room->WorldSnapshot.Systems)
	{
		if (System.StableKey.IsNone() || System.ContractVersion <= 0 || System.SaveGameBytes.Num() > MaxRecordBytes
			|| SystemKeys.Contains(System.StableKey))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SystemValidation Result=Failed Key=%s Contract=%d Bytes=%d Reason=InvalidKeyContractPayloadOrDuplicate"),
				*System.StableKey.ToString(), System.ContractVersion, System.SaveGameBytes.Num());
			return false;
		}
		SystemKeys.Add(System.StableKey);
	}
	for (const FGuid& Id : Room->WorldSnapshot.ReferenceIds)
		if (!ActorIds.Contains(Id))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=ReferenceValidation Result=Failed Id=%s Reason=TargetActorMissing"), *Id.ToString());
			return false;
		}
	TSet<FString> IssueKeys;
	auto ValidateIssues = [&IssueKeys](const TArray<FSWRoomCaptureIssue>& Issues)
	{
		if (Issues.Num() > MaxActorRecords)
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=IssueValidation Result=Failed Count=%d Reason=TooManyIssues"), Issues.Num());
			return false;
		}
		for (const FSWRoomCaptureIssue& Issue : Issues)
		{
			if (Issue.Domain.IsNone() || Issue.FieldKey.IsNone() || Issue.Reason.IsEmpty())
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=IssueValidation Result=Failed Id=%s Domain=%s Field=%s Reason=IncompleteIssue"),
					*Issue.StableId.ToString(), *Issue.Domain.ToString(), *Issue.FieldKey.ToString());
				return false;
			}
			if (Issue.Scope == ESWRoomIssueScope::WorldActor
				&& ((!Issue.StableId.IsValid() && Issue.OwnerPath.IsEmpty())
					|| (Issue.ClassPath.IsNull() && Issue.Domain != TEXT("Partition"))))
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=IssueValidation Result=Failed Id=%s Actor=%s Domain=%s Field=%s Reason=WorldIssueMissingIdentity"),
					*Issue.StableId.ToString(), *Issue.OwnerPath, *Issue.Domain.ToString(), *Issue.FieldKey.ToString());
				return false;
			}
			if (Issue.Scope == ESWRoomIssueScope::Player && Issue.PlayerKey.IsEmpty())
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=IssueValidation Result=Failed Domain=%s Field=%s Reason=PlayerIssueMissingKey"),
					*Issue.Domain.ToString(), *Issue.FieldKey.ToString());
				return false;
			}
			const FString Owner = Issue.StableId.IsValid() ? Issue.StableId.ToString()
				: !Issue.PlayerKey.IsEmpty() ? Issue.PlayerKey : Issue.OwnerPath;
			const FString Key = FString::Printf(TEXT("%d|%s|%s|%s"), static_cast<int32>(Issue.Scope),
				*Owner, *Issue.Domain.ToString(), *Issue.FieldKey.ToString());
			if (IssueKeys.Contains(Key))
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=IssueValidation Result=Failed Key=%s Reason=DuplicateIssue"), *Key);
				return false;
			}
			IssueKeys.Add(Key);
		}
		return true;
	};
	if (!ValidateIssues(Room->WorldSnapshot.CaptureIssues)
		|| !ValidateIssues(Room->HostProgress.CaptureIssues)
		|| !ValidateIssues(Room->SharedProgress.CaptureIssues)) return Fail(TEXT("InvalidWorldHostOrSharedIssue"));
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
		if (!ValidateIssues(Guest.Progress.CaptureIssues)) return Fail(TEXT("InvalidGuestIssue"));
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.Actors)
		if (Record.AttachParentId.IsValid() && !ActorIds.Contains(Record.AttachParentId))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=AttachmentValidation Result=Failed ActorId=%s ParentId=%s Reason=ParentRecordMissing"),
				*Record.StableId.ToString(), *Record.AttachParentId.ToString());
			return false;
		}
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.UnloadedActors)
		if (Record.AttachParentId.IsValid() && !ActorIds.Contains(Record.AttachParentId))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=AttachmentValidation Result=Failed ActorId=%s ParentId=%s Reason=ParentRecordMissing"),
				*Record.StableId.ToString(), *Record.AttachParentId.ToString());
			return false;
		}
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.Actors)
		for (const FGuid& Id : Record.ReferenceIds)
			if (!ActorIds.Contains(Id))
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=ReferenceValidation Result=Failed ActorId=%s TargetId=%s Reason=TargetActorMissing"),
					*Record.StableId.ToString(), *Id.ToString());
				return false;
			}
	for (const FSWRoomActorRecord& Record : Room->WorldSnapshot.UnloadedActors)
		for (const FGuid& Id : Record.ReferenceIds)
			if (!ActorIds.Contains(Id))
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=ReferenceValidation Result=Failed ActorId=%s TargetId=%s Reason=TargetActorMissing"),
					*Record.StableId.ToString(), *Id.ToString());
				return false;
			}
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
	if (!ValidPlayer(Room->HostProgress))
	{
		UE_LOG(LogSWRoomSave, Error,
			TEXT("Flow=PlayerValidation Result=Failed Role=Host Key=%s Inventory=%d QuickSlots=%d Upgrades=%d Skills=%d Health=%g MaxHealth=%g Reason=InvalidCountStatOrSlot"),
			*Room->HostDisplayName, Room->HostProgress.InventorySlots.Num(), Room->HostProgress.QuickSlotItemTags.Num(),
			Room->HostProgress.UpgradeNodeIds.Num(), Room->HostProgress.Skills.Num(),
			Room->HostProgress.CurrentHealth, Room->HostProgress.MaximumHealth);
		return false;
	}
	FString Normalized;
	TSet<FString> GuestNames;
	for (const FSWRoomGuestProgress& Guest : Room->Guests)
	{
		if (!FSWRoomName::Normalize(Guest.DisplayName, Normalized) || Normalized != Guest.DisplayName
			|| Guest.DisplayName == Room->HostDisplayName || GuestNames.Contains(Guest.DisplayName)
			|| !ValidPlayer(Guest.Progress))
		{
			UE_LOG(LogSWRoomSave, Error,
				TEXT("Flow=PlayerValidation Result=Failed Role=Guest Key=%s Normalized=%s Duplicate=%d Inventory=%d QuickSlots=%d Skills=%d Reason=InvalidNameCountStatOrSlot"),
				*Guest.DisplayName, *Normalized, GuestNames.Contains(Guest.DisplayName),
				Guest.Progress.InventorySlots.Num(), Guest.Progress.QuickSlotItemTags.Num(), Guest.Progress.Skills.Num());
			return false;
		}
		GuestNames.Add(Guest.DisplayName);
	}
	TSet<FString> StorageKeys;
	for (const FSWRoomStorageProgress& Storage : Room->SharedProgress.Storage)
	{
		if (!Storage.ChestId.IsValid() || Storage.SaveNamespace.Len() > 128 || Storage.SlotsPerTab < 1
			|| Storage.SlotsPerTab > 10000 || Storage.Slots.Num() != Storage.SlotsPerTab * 4
			|| Storage.Slots.Num() > MaxStorageSlots)
		{
			UE_LOG(LogSWRoomSave, Error,
				TEXT("Flow=StorageValidation Result=Failed ChestId=%s Namespace=%s SlotsPerTab=%d Slots=%d Reason=InvalidIdNamespaceOrSlotCount"),
				*Storage.ChestId.ToString(), *Storage.SaveNamespace, Storage.SlotsPerTab, Storage.Slots.Num());
			return false;
		}
		const FString Key = Storage.ChestId.ToString() + TEXT("|") + Storage.SaveNamespace;
		if (StorageKeys.Contains(Key))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=StorageValidation Result=Failed Key=%s Reason=DuplicateStorageKey"), *Key);
			return false;
		}
		StorageKeys.Add(Key);
		for (int32 Index = 0; Index < Storage.Slots.Num(); ++Index)
			if (Storage.Slots[Index].Count < 0)
			{
				UE_LOG(LogSWRoomSave, Error, TEXT("Flow=StorageValidation Result=Failed Key=%s Slot=%d Count=%d Reason=NegativeCount"),
					*Key, Index, Storage.Slots[Index].Count);
				return false;
			}
	}
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
		TEXT("Flow=SaveValidation Result=Success RoomId=%s Sequence=%llu Actors=%d Unloaded=%d Guests=%d SharedStorage=%d Issues=%d"),
		*Room->RoomId.ToString(), Room->CaptureSequence, Room->WorldSnapshot.Actors.Num(),
		Room->WorldSnapshot.UnloadedActors.Num(), Room->Guests.Num(), Room->SharedProgress.Storage.Num(),
		Room->WorldSnapshot.CaptureIssues.Num() + Room->HostProgress.CaptureIssues.Num() + Room->SharedProgress.CaptureIssues.Num());
	return true;
}

bool FSWRoomSaveStore::ValidateHeader(const USWRoomSaveGame* Room)
{
	if (!Room || Room->SaveVersion != USWRoomSaveGame::CurrentVersion || !Room->RoomId.IsValid()
		|| Room->Guests.Num() > MaxGuests
		|| Room->ContentContractVersion != USWRoomSaveGame::CurrentContentContractVersion)
	{
		UE_LOG(LogSWRoomSave, Error,
			TEXT("Flow=HeaderValidation Result=Failed RoomId=%s Version=%d ExpectedVersion=%d Content=%d ExpectedContent=%d Guests=%d Reason=MissingRoomInvalidVersionContractOrGuestCount"),
			Room ? *Room->RoomId.ToString() : TEXT("None"), Room ? Room->SaveVersion : -1,
			USWRoomSaveGame::CurrentVersion, Room ? Room->ContentContractVersion : -1,
			USWRoomSaveGame::CurrentContentContractVersion, Room ? Room->Guests.Num() : -1);
		return false;
	}
	FString Normalized;
	if (!FSWRoomName::Normalize(Room->HostDisplayName, Normalized) || Normalized != Room->HostDisplayName)
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=HeaderValidation Result=Failed RoomId=%s Host=%s Normalized=%s Reason=InvalidHostName"),
			*Room->RoomId.ToString(), *Room->HostDisplayName, *Normalized);
		return false;
	}
	return true;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadPath(UObject* Outer, const FString& Path, bool bHeaderOnly)
{
	if (Path.IsEmpty()) return nullptr;
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("SaveStore.LoadPath"));
	auto FailRead = [&Path, bHeaderOnly](const TCHAR* Reason) -> USWRoomSaveGame*
	{
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileRead Result=Unavailable Path=%s HeaderOnly=%d Reason=%s"), *Path, bHeaderOnly, Reason);
		return nullptr;
	};
	TArray<uint8> Bytes;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const int64 Size = Files.FileSize(*Path);
	if (Size <= 0 || Size > MaxRoomFileBytes) return FailRead(TEXT("MissingEmptyOrOversized"));
	{
		SWRoomLoadDiagnostics::FScopedPhase ReadScope(TEXT("SaveStore.DiskRead"));
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return FailRead(TEXT("DiskRead"));
	}
	if (Bytes.Num() < HeaderBytes) return FailRead(TEXT("TruncatedHeader"));
	uint32 Magic = 0;
	uint64 PayloadSize = 0;
	uint32 Checksum = 0;
	FMemory::Memcpy(&Magic, Bytes.GetData(), sizeof(Magic));
	FMemory::Memcpy(&PayloadSize, Bytes.GetData() + sizeof(Magic), sizeof(PayloadSize));
	FMemory::Memcpy(&Checksum, Bytes.GetData() + sizeof(Magic) + sizeof(PayloadSize), sizeof(Checksum));
	if (Magic != RoomMagic || PayloadSize != static_cast<uint64>(Bytes.Num() - HeaderBytes)
		|| FCrc::MemCrc32(Bytes.GetData() + HeaderBytes, static_cast<int32>(PayloadSize)) != Checksum) return FailRead(TEXT("MagicSizeOrChecksum"));
	TArray<uint8> Payload;
	Payload.Append(Bytes.GetData() + HeaderBytes, static_cast<int32>(PayloadSize));
	USWRoomSaveGame* Room;
	{
		SWRoomLoadDiagnostics::FScopedPhase DeserializeScope(TEXT("SaveStore.Deserialize"));
		Room = Cast<USWRoomSaveGame>(UGameplayStatics::LoadGameFromMemory(Payload));
	}
	{
		SWRoomLoadDiagnostics::FScopedPhase ValidateScope(TEXT("SaveStore.ValidateReadback"));
		if (!(bHeaderOnly ? ValidateHeader(Room) : Validate(Room))) return FailRead(TEXT("DeserializeOrValidation"));
	}
	if (Outer && Room) Room->Rename(nullptr, Outer);
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
		TEXT("Flow=FileRead Result=Validated RoomId=%s Sequence=%llu Path=%s HeaderOnly=%d Bytes=%d Checksum=%u Actors=%d Unloaded=%d Systems=%d Guests=%d"),
		*Room->RoomId.ToString(), Room->CaptureSequence, *Path, bHeaderOnly, Bytes.Num(), Checksum,
		Room->WorldSnapshot.Actors.Num(), Room->WorldSnapshot.UnloadedActors.Num(), Room->WorldSnapshot.Systems.Num(), Room->Guests.Num());
	return Room;
}

USWRoomSaveGame* FSWRoomSaveStore::LoadCurrentRoom(UObject* Outer)
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return nullptr;
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
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	return !Files.FileExists(*RoomPath()) && Files.FileExists(*SidecarPath(TEXT("CurrentRoom.sav")));
}

bool FSWRoomSaveStore::WriteVerified(const USWRoomSaveGame* Room, const FString& Path, bool bHeaderOnly)
{
	if (Path.IsEmpty()) return false;
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("SaveStore.WriteVerified"));
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileWrite Phase=Begin RoomId=%s Sequence=%llu Path=%s HeaderOnly=%d"),
		Room ? *Room->RoomId.ToString() : TEXT("None"), Room ? Room->CaptureSequence : 0, *Path, bHeaderOnly);
	if (!(bHeaderOnly ? ValidateHeader(Room) : Validate(Room)))
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileWrite Result=Failed Phase=Validate Path=%s"), *Path);
		return false;
	}
	TArray<uint8> Bytes;
	bool bSerialized;
	{
		SWRoomLoadDiagnostics::FScopedPhase SerializeScope(TEXT("SaveStore.Serialize"));
		bSerialized = UGameplayStatics::SaveGameToMemory(const_cast<USWRoomSaveGame*>(Room), Bytes);
	}
	if (!bSerialized
		|| Bytes.IsEmpty() || Bytes.Num() > MaxRoomFileBytes - HeaderBytes)
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileWrite Result=Failed Phase=Serialize Path=%s Bytes=%d MaxBytes=%lld"),
			*Path, Bytes.Num(), MaxRoomFileBytes - HeaderBytes);
		return false;
	}
	const uint64 PayloadSize = Bytes.Num();
	const uint32 Checksum = FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
	TArray<uint8> FileBytes;
	FileBytes.SetNumUninitialized(HeaderBytes + Bytes.Num());
	FMemory::Memcpy(FileBytes.GetData(), &RoomMagic, sizeof(RoomMagic));
	FMemory::Memcpy(FileBytes.GetData() + sizeof(RoomMagic), &PayloadSize, sizeof(PayloadSize));
	FMemory::Memcpy(FileBytes.GetData() + sizeof(RoomMagic) + sizeof(PayloadSize), &Checksum, sizeof(Checksum));
	FMemory::Memcpy(FileBytes.GetData() + HeaderBytes, Bytes.GetData(), Bytes.Num());
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	{
	SWRoomLoadDiagnostics::FScopedPhase DiskWriteScope(TEXT("SaveStore.DiskWrite"));
	if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)) || !FFileHelper::SaveArrayToFile(FileBytes, *Path))
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileWrite Result=Failed Phase=DiskWrite Path=%s Bytes=%d"), *Path, FileBytes.Num());
		return false;
	}
	}
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileWrite Phase=Written Path=%s Bytes=%d Checksum=%u"),
		*Path, FileBytes.Num(), Checksum);
	USWRoomSaveGame* Checked = LoadPath(GetTransientPackage(), Path, bHeaderOnly);
	const bool bVerified = Checked && Checked->RoomId == Room->RoomId && Checked->SaveVersion == Room->SaveVersion
		&& Checked->CaptureSequence == Room->CaptureSequence && Checked->MapPath == Room->MapPath;
	if (bVerified)
	{
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileWrite Result=Verified Phase=Readback RoomId=%s Sequence=%llu Path=%s"),
			*Room->RoomId.ToString(), Room->CaptureSequence, *Path);
	}
	else
	{
		UE_LOG(LogSWRoomSave, Error,
			TEXT("Flow=FileWrite Result=Failed Phase=Readback RoomId=%s Sequence=%llu Path=%s Loaded=%d LoadedRoomId=%s LoadedSequence=%llu"),
			*Room->RoomId.ToString(), Room->CaptureSequence, *Path, Checked != nullptr,
			Checked ? *Checked->RoomId.ToString() : TEXT("None"), Checked ? Checked->CaptureSequence : 0);
	}
	return bVerified;
}

bool FSWRoomSaveStore::WriteCurrentRoom(const USWRoomSaveGame* Room)
{
	return WriteCurrentRoomInternal(Room, false);
}

bool FSWRoomSaveStore::WriteCurrentRoomInternal(const USWRoomSaveGame* Room, bool bAllowInvalidCurrent)
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return false;
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("SaveStore.Transaction"));
	const FString Path = RoomPath();
	const FString Temp = SidecarPath(TEXT("CurrentRoom.v4.tmp"));
	const FString Backup = SidecarPath(TEXT("CurrentRoom.v4.bak"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
		TEXT("Flow=FileTransaction Phase=Begin RoomId=%s Sequence=%llu Current=%s Temp=%s Backup=%s AllowInvalidCurrent=%d"),
		Room ? *Room->RoomId.ToString() : TEXT("None"), Room ? Room->CaptureSequence : 0,
		*Path, *Temp, *Backup, bAllowInvalidCurrent ? 1 : 0);
	const bool bHasInvalidCurrent = Files.FileExists(*Path) && !LoadPath(GetTransientPackage(), Path);
	if (bHasInvalidCurrent && !bAllowInvalidCurrent)
	{
		const USWRoomSaveGame* Recovery = LoadPath(GetTransientPackage(), Backup);
		if (!Recovery || Recovery->RoomId != Room->RoomId)
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=Recovery Reason=InvalidCurrentWithoutMatchingBackup Current=%s Backup=%s"), *Path, *Backup);
			return false;
		}
	}
	if (bHasInvalidCurrent)
	{
		const FString CorruptPath = SidecarPath(*FString::Printf(TEXT("CurrentRoom.v4.corrupt.%lld.sav"), FDateTime::UtcNow().ToUnixTimestamp()));
		if (!Files.CopyFile(*CorruptPath, *Path))
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=PreserveInvalidCurrent Source=%s Destination=%s"), *Path, *CorruptPath);
			return false;
		}
		UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=FileTransaction Phase=InvalidCurrentPreserved Source=%s Destination=%s"), *Path, *CorruptPath);
	}
	Files.DeleteFile(*Temp);
	if (!WriteVerified(Room, Temp))
	{
		Files.DeleteFile(*Temp);
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=TemporaryWrite Path=%s"), *Temp);
		return false;
	}
	const bool bHadValidCurrent = LoadPath(GetTransientPackage(), Path) != nullptr;
	const bool bHadValidBackup = LoadPath(GetTransientPackage(), Backup) != nullptr;
	if (bHadValidCurrent)
	{
		Files.DeleteFile(*Backup);
		if (!Files.CopyFile(*Backup, *Path) || !LoadPath(GetTransientPackage(), Backup))
		{
			Files.DeleteFile(*Temp);
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=BackupWrite Path=%s"), *Backup);
			return false;
		}
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileTransaction Phase=BackupVerified Path=%s"), *Backup);
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
		if (Checked && Checked->RoomId == Room->RoomId && Checked->CaptureSequence == Room->CaptureSequence)
		{
			SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=FileTransaction Result=Committed RoomId=%s Sequence=%llu Path=%s"),
				*Room->RoomId.ToString(), Room->CaptureSequence, *Path);
			return true;
		}
		if (bHadValidCurrent || bHadValidBackup)
		{
#if PLATFORM_WINDOWS
			::CopyFileW(*Backup, *Path, false);
#else
			Files.CopyFile(*Path, *Backup);
#endif
		}
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=FinalReadback Path=%s RollbackAvailable=%d"),
			*Path, bHadValidCurrent || bHadValidBackup);
		return false;
	}
	Files.DeleteFile(*Temp);
	UE_LOG(LogSWRoomSave, Error, TEXT("Flow=FileTransaction Result=Failed Phase=Replace Temp=%s Current=%s"), *Temp, *Path);
	return false;
}

bool FSWRoomSaveStore::StageNewRoom(const USWRoomSaveGame* Room)
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return false;
	const FString Path = SidecarPath(TEXT("CurrentRoom.v4.pending.sav"));
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*Path);
	return Room && !Room->bComplete && WriteVerified(Room, Path, true);
}

bool FSWRoomSaveStore::StageCompleteNewRoom(const USWRoomSaveGame* Room)
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return false;
	return WriteVerified(Room, SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
}

USWRoomSaveGame* FSWRoomSaveStore::LoadStagedNewRoom(UObject* Outer)
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return nullptr;
	return LoadPath(Outer, SidecarPath(TEXT("CurrentRoom.v4.pending.sav")), true);
}

bool FSWRoomSaveStore::CommitStagedNewRoom()
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return false;
	USWRoomSaveGame* Pending = LoadPath(GetTransientPackage(), SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
	if (!Pending || Pending->SaveKind != ESWRoomSaveKind::New || !WriteCurrentRoomInternal(Pending, true)) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.DeleteFile(*SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
	return true;
}

void FSWRoomSaveStore::DiscardStagedNewRoom()
{
	if (FSWRoomRuntimePaths::GetSaveDirectory().IsEmpty()) return;
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*SidecarPath(TEXT("CurrentRoom.v4.pending.sav")));
}
