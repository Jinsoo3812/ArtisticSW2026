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
	UPROPERTY(SaveGame) FGameplayTag EquippedItemTag;
	UPROPERTY(SaveGame) TArray<FName> UpgradeNodeIds;
	UPROPERTY(SaveGame) TArray<FSWRoomSkillProgress> Skills;
	UPROPERTY(SaveGame) bool bHasResumeTransform = false;
	UPROPERTY(SaveGame) FTransform ResumeWorldTransform;
	UPROPERTY(SaveGame) FTransform ShipRelativeTransform;
	UPROPERTY(SaveGame) FGuid ShipStableId;
	UPROPERTY(SaveGame) FRotator ControlRotation = FRotator::ZeroRotator;
	UPROPERTY(SaveGame) FName CameraMode;
	UPROPERTY(SaveGame) float CameraZoom = 0.f;
	UPROPERTY(SaveGame) bool bWasDead = false;
	UPROPERTY(SaveGame) bool bWasSwimming = false;
	UPROPERTY(SaveGame) bool bWasMounted = false;
	UPROPERTY(SaveGame) bool bHasMovement = false;
	UPROPERTY(SaveGame) FVector WorldVelocity = FVector::ZeroVector;
	UPROPERTY(SaveGame) uint8 MovementMode = 0;
	UPROPERTY(SaveGame) uint8 CustomMovementMode = 0;
	UPROPERTY(SaveGame) bool bEffectsCaptured = false;
	UPROPERTY(SaveGame) TArray<FSWRoomGameplayEffectState> ActiveEffects;
	UPROPERTY(SaveGame) FGuid MountedDeviceId;
	UPROPERTY(SaveGame) float CurrentHealth = 0.f;
	UPROPERTY(SaveGame) float MaximumHealth = 0.f;
	UPROPERTY(SaveGame) float BaseStrength = 0.f;
	UPROPERTY(SaveGame) float BaseMoveSpeed = 0.f;
	UPROPERTY(SaveGame) float BaseMoveSpeedMultiplier = 1.f;
	UPROPERTY(SaveGame) float BaseAttackSpeedMultiplier = 1.f;
	UPROPERTY(SaveGame) TArray<FSWRoomCaptureIssue> CaptureIssues;
	bool bRestoreFullHealth = false;
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
	UPROPERTY(SaveGame) TArray<FSWRoomCaptureIssue> CaptureIssues;
};

UCLASS()
class ARTISTICSWCORE_API USWRoomSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentVersion = 7;
	static constexpr int32 CurrentContentContractVersion = 3;
	UPROPERTY(SaveGame) int32 SaveVersion = CurrentVersion;
	UPROPERTY(SaveGame) ESWRoomSaveKind SaveKind = ESWRoomSaveKind::New;
	UPROPERTY(SaveGame) uint64 CaptureSequence = 0;
	UPROPERTY(SaveGame) FDateTime SavedAtUtc;
	UPROPERTY(SaveGame) int32 ContentContractVersion = 1;
	UPROPERTY(SaveGame) FSoftObjectPath MapPath;
	UPROPERTY(SaveGame) bool bComplete = false;
	UPROPERTY(Transient) bool bRecoveredFromBackup = false;
	UPROPERTY(SaveGame) FSWRoomWorldSnapshot WorldSnapshot;
	UPROPERTY(SaveGame) FGuid RoomId;
	UPROPERTY(SaveGame) FString HostDisplayName;
	UPROPERTY(SaveGame) FSWRoomPlayerProgress HostProgress;
	UPROPERTY(SaveGame) TArray<FSWRoomGuestProgress> Guests;
	UPROPERTY(SaveGame) FSWRoomSharedProgress SharedProgress;
};
