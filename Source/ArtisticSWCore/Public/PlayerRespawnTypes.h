#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PlayerRespawnTypes.generated.h"

class AActor;

USTRUCT()
struct ARTISTICSWCORE_API FSWSkillStateSnapshot
{
	GENERATED_BODY()
	UPROPERTY() FGameplayTag SkillTag;
	UPROPERTY() bool bUnlocked = false;
	UPROPERTY() bool bConditionsMet = false;
};

UENUM(BlueprintType)
enum class ESWPlayerSlot : uint8
{
	Player0,
	Player1,
	Any = 255
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWInventorySlotSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Tab = 0;

	UPROPERTY()
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY()
	FGameplayTag ItemTag;

	UPROPERTY()
	int32 Count = 0;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWPlayerProgressSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSWInventorySlotSnapshot> InventorySlots;

	UPROPERTY()
	TArray<FName> ActiveShipUpgradeNodeIds;
	UPROPERTY() TArray<FGameplayTag> QuickSlotItemTags;
	UPROPERTY() TArray<FSWSkillStateSnapshot> Skills;

	UPROPERTY()
	float CurrentHealth = 0.0f;

	UPROPERTY()
	bool bHasCurrentHealth = false;

	UPROPERTY()
	bool bWasDead = false;

	UPROPERTY()
	FTransform LastValidWorldTransform = FTransform::Identity;

	UPROPERTY()
	bool bHasLastValidWorldTransform = false;

	UPROPERTY()
	TWeakObjectPtr<AActor> LastMovementHost;

	UPROPERTY()
	FTransform LastMovementHostRelativeTransform = FTransform::Identity;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWReconnectRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ReconnectToken;

	UPROPERTY()
	int32 PlayerIndex = INDEX_NONE;

	UPROPERTY()
	FSWPlayerProgressSnapshot Snapshot;

	UPROPERTY()
	bool bHasSnapshot = false;

	double DisconnectTimeSeconds = 0.0;
	double ExpirationTimeSeconds = 0.0;
	bool bConnectionActive = false;
};
