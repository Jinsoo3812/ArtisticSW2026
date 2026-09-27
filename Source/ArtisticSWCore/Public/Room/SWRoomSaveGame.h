#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "PlayerRespawnTypes.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "SWRoomSaveGame.generated.h"

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomSkillProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGameplayTag SkillTag;
	UPROPERTY(SaveGame) bool bUnlocked = false;
	UPROPERTY(SaveGame) bool bConditionsMet = false;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomPlayerProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FSWInventorySlotSnapshot> InventorySlots;
	UPROPERTY(SaveGame) TArray<FGameplayTag> QuickSlotItemTags;
	UPROPERTY(SaveGame) TArray<FName> UpgradeNodeIds;
	UPROPERTY(SaveGame) TArray<FSWRoomSkillProgress> Skills;
	UPROPERTY(SaveGame) bool bHasResumeTransform = false;
	UPROPERTY(SaveGame) FTransform ResumeWorldTransform;
	UPROPERTY(SaveGame) FTransform ShipRelativeTransform;
	UPROPERTY(SaveGame) FGuid ShipStableId;
	UPROPERTY(SaveGame) FRotator ControlRotation;
	UPROPERTY(SaveGame) FName CameraMode;
	UPROPERTY(SaveGame) float CameraZoom = 0.f;
	UPROPERTY(SaveGame) bool bWasDead = false;
	UPROPERTY(SaveGame) bool bWasSwimming = false;
	UPROPERTY(SaveGame) bool bWasMounted = false;
	UPROPERTY(SaveGame) FGuid MountedDeviceId;
	UPROPERTY(SaveGame) float CurrentHealth = 0.f;
	UPROPERTY(SaveGame) float MaximumHealth = 0.f;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomGuestProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FString DisplayName;
	UPROPERTY(SaveGame) FSWRoomPlayerProgress Progress;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomCounterProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGameplayTag Tag;
	UPROPERTY(SaveGame) int32 Value = 0;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomStorageSlot
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGameplayTag ItemTag;
	UPROPERTY(SaveGame) int32 Count = 0;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomStorageProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid ChestId;
	UPROPERTY(SaveGame) FString SaveNamespace;
	UPROPERTY(SaveGame) int32 SlotsPerTab = 0;
	UPROPERTY(SaveGame) TArray<FSWRoomStorageSlot> Slots;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWRoomSharedProgress
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FPrimaryAssetId StoryDefinitionId;
	UPROPERTY(SaveGame) FGameplayTagContainer Facts;
	UPROPERTY(SaveGame) TArray<FSWRoomCounterProgress> Counters;
	UPROPERTY(SaveGame) TArray<FName> AppliedActionKeys;
	UPROPERTY(SaveGame) TArray<FName> ShipUpgradeNodeIds;
	UPROPERTY(SaveGame) TArray<FSWRoomStorageProgress> Storage;
};

UCLASS()
class ARTISTICSWCORE_API USWRoomSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 SaveVersion = 2;
	UPROPERTY(SaveGame) ESWRoomSaveKind SaveKind = ESWRoomSaveKind::New;
	UPROPERTY(SaveGame) uint64 CaptureSequence = 0;
	UPROPERTY(SaveGame) FSWRoomWorldSnapshot WorldSnapshot;
	UPROPERTY(SaveGame) FGuid RoomId;
	UPROPERTY(SaveGame) FString HostDisplayName;
	UPROPERTY(SaveGame) FSWRoomPlayerProgress HostProgress;
	UPROPERTY(SaveGame) TArray<FSWRoomGuestProgress> Guests;
	UPROPERTY(SaveGame) FSWRoomSharedProgress SharedProgress;
};
