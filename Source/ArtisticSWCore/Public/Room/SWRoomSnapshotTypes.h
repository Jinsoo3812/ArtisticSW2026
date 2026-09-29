#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagContainer.h"
#include "SWRoomSnapshotTypes.generated.h"

UENUM()
enum class ESWRoomSaveKind : uint8
{
	New,
	Manual,
	Return
};

UENUM()
enum class ESWRoomCaptureFailureKind : uint8
{
	None,
	StateUnavailable,
	Structural
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

UENUM()
enum class ESWRoomIssueScope : uint8
{
	WorldActor,
	Player,
	Shared
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomCaptureIssue
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) ESWRoomIssueScope Scope = ESWRoomIssueScope::WorldActor;
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FString OwnerPath;
	UPROPERTY(SaveGame) FString PlayerKey;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) FName Domain;
	UPROPERTY(SaveGame) FName FieldKey;
	UPROPERTY(SaveGame) FString Reason;
};

UENUM()
enum class ESWRoomDomain : uint8
{
	Ship = 1,
	Enemy = 2,
	Chest = 3,
	Spawner = 4,
	Projectile = 5,
	Area = 6,
	Loot = 7,
	Boss = 8
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomDomainPart
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) ESWRoomDomain Domain = ESWRoomDomain::Ship;
	UPROPERTY(SaveGame) int32 Version = 0;
	UPROPERTY(SaveGame) TArray<uint8> Bytes;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomDomainPayload
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FSWRoomDomainPart> Parts;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomSetByCallerTagValue
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGameplayTag Tag;
	UPROPERTY(SaveGame) float Value = 0.f;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomSetByCallerNameValue
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName Name;
	UPROPERTY(SaveGame) float Value = 0.f;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomGameplayEffectState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftClassPath EffectClass;
	UPROPERTY(SaveGame) float Level = 1.f;
	UPROPERTY(SaveGame) int32 StackCount = 1;
	UPROPERTY(SaveGame) float DurationRemaining = -1.f;
	UPROPERTY(SaveGame) float NextPeriodRemaining = -1.f;
	UPROPERTY(SaveGame) TArray<FSWRoomSetByCallerTagValue> TagMagnitudes;
	UPROPERTY(SaveGame) TArray<FSWRoomSetByCallerNameValue> NameMagnitudes;
	UPROPERTY(SaveGame) FGuid SourceStableId;
	UPROPERTY(SaveGame) FSoftClassPath SourceClass;
	UPROPERTY(SaveGame) FGameplayTagContainer CapturedSourceTags;
	UPROPERTY(SaveGame) bool bSourceMissingAllowed = true;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomPendingSpawnTicket
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) int32 GroupIndex = INDEX_NONE;
	UPROPERTY(SaveGame) int32 Ordinal = INDEX_NONE;
	UPROPERTY(SaveGame) FSoftClassPath EnemyClass;
	UPROPERTY(SaveGame) FSoftObjectPath StatsTable;
	UPROPERTY(SaveGame) FName StatsRow;
	UPROPERTY(SaveGame) FTransform WorldTransform;
	UPROPERTY(SaveGame) FGuid ReservedId;
	UPROPERTY(SaveGame) float RemainingSeconds = 0.f;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomLevelPartition
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath PackagePath;
	UPROPERTY(SaveGame) FName InstanceName;
	UPROPERTY(SaveGame) FGuid InstanceId;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomMotionState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) bool bHasMotion = false;
	UPROPERTY(SaveGame) FVector LinearVelocity = FVector::ZeroVector;
	UPROPERTY(SaveGame) FVector AngularVelocityDegrees = FVector::ZeroVector;
	UPROPERTY(SaveGame) bool bWasSimulatingPhysics = false;
	UPROPERTY(SaveGame) bool bWasProjectileMovementActive = false;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomComponentRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName StableKey;
	UPROPERTY(SaveGame) TArray<uint8> SaveGameBytes;
	UPROPERTY(SaveGame) FTransform WorldTransform;
	UPROPERTY(SaveGame) FSWRoomMotionState MotionState;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomActorRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) FSWRoomLevelPartition LevelPartition;
	UPROPERTY(SaveGame) ESWRoomSpawnOrigin Origin = ESWRoomSpawnOrigin::LevelPlaced;
	UPROPERTY(SaveGame) ESWRoomPersistenceClass PersistenceClass = ESWRoomPersistenceClass::ManualOnly;
	UPROPERTY(SaveGame) int32 ContractVersion = 1;
	UPROPERTY(SaveGame) bool bRequired = false;
	UPROPERTY(SaveGame) FGuid CreatorId;
	UPROPERTY(SaveGame) uint64 CreatorSequence = 0;
	UPROPERTY(SaveGame) FTransform WorldTransform;
	UPROPERTY(SaveGame) FGuid AttachParentId;
	UPROPERTY(SaveGame) FName AttachParentComponentName;
	UPROPERTY(SaveGame) FName AttachSocketName;
	UPROPERTY(SaveGame) FSWRoomMotionState MotionState;
	UPROPERTY(SaveGame) TArray<uint8> SaveGameBytes;
	UPROPERTY(SaveGame) TArray<FSWRoomComponentRecord> Components;
	UPROPERTY(SaveGame) int32 AdapterVersion = 0;
	UPROPERTY(SaveGame) TArray<uint8> AdapterBytes;
	UPROPERTY(SaveGame) FName AdapterType;
	UPROPERTY(SaveGame) TArray<FGuid> ReferenceIds;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomSystemRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName StableKey;
	UPROPERTY(SaveGame) int32 ContractVersion = 1;
	UPROPERTY(SaveGame) TArray<uint8> SaveGameBytes;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomDestroyedActorPartition
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FSoftObjectPath PackagePath;
	UPROPERTY(SaveGame) FName InstanceName;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomWorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath MapPath;
	UPROPERTY(SaveGame) TArray<FSWRoomActorRecord> Actors;
	UPROPERTY(SaveGame) TArray<FGuid> DestroyedLevelActorIds;
	UPROPERTY(SaveGame) TArray<FSWRoomDestroyedActorPartition> DestroyedActorPartitions;
	UPROPERTY(SaveGame) TArray<FSWRoomActorRecord> UnloadedActors;
	UPROPERTY(SaveGame) TArray<FSWRoomSystemRecord> Systems;
	UPROPERTY(SaveGame) TArray<FGuid> ReferenceIds;
	UPROPERTY(SaveGame) uint64 CaptureSequence = 0;
	UPROPERTY(SaveGame) TArray<FSWRoomCaptureIssue> CaptureIssues;
};
