#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Inventory/InventoryComponent.h"
#include "SharedStorageSaveGame.generated.h"

UCLASS()
class CLASSFEATURE_API USharedStorageSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() int32 SlotsPerTab = 25;
	UPROPERTY() TArray<FInventorySlot> Slots;
};
