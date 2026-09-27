#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PlayerRespawnTypes.generated.h"

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
};
