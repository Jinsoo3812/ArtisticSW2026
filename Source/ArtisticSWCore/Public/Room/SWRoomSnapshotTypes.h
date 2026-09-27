#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "SWRoomSnapshotTypes.generated.h"

UENUM()
enum class ESWRoomSaveKind : uint8
{
	New,
	Manual,
	Return
};

UENUM()
enum class ESWRoomSpawnOrigin : uint8
{
	LevelPlaced,
	Runtime
};

UENUM()
enum class ESWRoomPersistenceClass : uint8
{
	ManualAndReturn,
	ManualOnly,
	Transient
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomComponentRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName StableKey;
	UPROPERTY(SaveGame) TArray<uint8> SaveGameBytes;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomActorRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) ESWRoomSpawnOrigin Origin = ESWRoomSpawnOrigin::LevelPlaced;
	UPROPERTY(SaveGame) ESWRoomPersistenceClass PersistenceClass = ESWRoomPersistenceClass::ManualOnly;
	UPROPERTY(SaveGame) FGuid CreatorId;
	UPROPERTY(SaveGame) uint64 CreatorSequence = 0;
	UPROPERTY(SaveGame) FTransform WorldTransform;
	UPROPERTY(SaveGame) TArray<uint8> SaveGameBytes;
	UPROPERTY(SaveGame) TArray<FSWRoomComponentRecord> Components;
	UPROPERTY(SaveGame) int32 AdapterVersion = 0;
	UPROPERTY(SaveGame) TArray<uint8> AdapterBytes;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomWorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath MapPath;
	UPROPERTY(SaveGame) TArray<FSWRoomActorRecord> Actors;
	UPROPERTY(SaveGame) TArray<FGuid> DestroyedLevelActorIds;
};
