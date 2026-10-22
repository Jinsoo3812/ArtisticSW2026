#pragma once

#include "CoreMinimal.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "StorageChestPoolTypes.generated.h"

USTRUCT()
struct FStorageChestPoolProperty
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName Object;
	UPROPERTY(SaveGame) FName Name;
	UPROPERTY(SaveGame) FString Value;
};

USTRUCT()
struct FStorageChestPoolMesh
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName Component;
	UPROPERTY(SaveGame) FSoftObjectPath Mesh;
	UPROPERTY(SaveGame) TArray<FSoftObjectPath> Materials;
	UPROPERTY(SaveGame) FTransform RelativeTransform;
	UPROPERTY(SaveGame) bool bCastShadow = true;
};

/** The spawn point owns the logical chest even when its physical Actor is in the pool. */
USTRUCT()
struct FStorageChestPoolPlacement
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FGuid CreatorId;
	UPROPERTY(SaveGame) uint64 CreatorSequence = 0;
	UPROPERTY(SaveGame) ESWRoomPersistenceClass Persistence = ESWRoomPersistenceClass::ManualOnly;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) FTransform Transform;
	UPROPERTY(SaveGame) TArray<FName> Tags;
	UPROPERTY(SaveGame) TArray<FStorageChestPoolProperty> Properties;
	UPROPERTY(SaveGame) TArray<FStorageChestPoolMesh> Meshes;
	UPROPERTY(SaveGame) FSWRoomDomainPart ChestState;
	UPROPERTY(SaveGame) float ActivationRange = 100000.f;
	UPROPERTY(SaveGame) bool bDormant = false;
};

/** Identity and transform travel together to distinguish reuse from late movement packets. */
USTRUCT()
struct FStorageChestPoolAssignment
{
	GENERATED_BODY()
	UPROPERTY() bool bManaged = false;
	UPROPERTY() FGuid StableId;
	UPROPERTY() FTransform Transform;
	UPROPERTY() FText StorageName;
	UPROPERTY() FText ActionText;
	UPROPERTY() FText LockedActionText;
};
