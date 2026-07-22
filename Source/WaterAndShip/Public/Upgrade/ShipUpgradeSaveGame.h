#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ShipUpgradeSaveGame.generated.h"

UCLASS()
class WATERANDSHIP_API UShipUpgradeSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 DataVersion = 1;

	UPROPERTY(SaveGame)
	TArray<FName> ActiveNodeIds;
};
