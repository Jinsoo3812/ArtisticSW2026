#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyShipAvoidanceSettings.generated.h"

/** Technical safety tuning kept separate from combat/navigation balance Data Assets. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Enemy Ship Collision Avoidance"))
class ENEMY_API UEnemyShipAvoidanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.05", Units = "s"))
	float EvaluationInterval = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.5", Units = "s"))
	float PredictionHorizon = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.05", Units = "s"))
	float PredictionStep = 0.25f;

	/** Added once between the two predicted oriented hull rectangles. */
	UPROPERTY(Config, EditAnywhere, Category = "Hull", meta = (ClampMin = "0.0", Units = "cm"))
	float HullSafetyMargin = 400.0f;

	/** Extra pair clearance per second of prediction to cover waves and model error. */
	UPROPERTY(Config, EditAnywhere, Category = "Hull", meta = (ClampMin = "0.0", Units = "cm/s"))
	float UncertaintyGrowthPerSecond = 40.0f;

	/** Full reverse is deliberately simple and gives the yielding ship real braking force. */
	UPROPERTY(Config, EditAnywhere, Category = "Maneuver", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
	float ReverseMoveInput = -1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Maneuver", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumManeuverTime = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Maneuver", meta = (ClampMin = "0.0", Units = "s"))
	float ClearConfirmationTime = 0.8f;

	UPROPERTY(Config, EditAnywhere, Category = "Squad", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaximumEvaluatedShips = 5;
};
