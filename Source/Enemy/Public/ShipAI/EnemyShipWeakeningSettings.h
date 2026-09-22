#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyShipWeakeningSettings.generated.h"

class UEnemyShipWeakeningData;

UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "Enemy Ship Weakening"))
class ENEMY_API UEnemyShipWeakeningSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "Ship|Weakening")
	TSoftObjectPtr<UEnemyShipWeakeningData> WeakeningData;
};
