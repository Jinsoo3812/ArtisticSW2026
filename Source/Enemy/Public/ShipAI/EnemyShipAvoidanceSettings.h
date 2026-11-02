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
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyEvaluationInterval = 0.1f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float TerrainForwardProbeInterval = 0.2f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyPredictionTime = 1.5f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyPredictionStep = 0.25f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyMaximumYawStep = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") int32 SafetyMaximumSegments = 32;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyMargin = 150.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyLatencyAllowance = 0.2f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float SafetyMinimumProbeDistance = 300.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float RetryMinimum = 0.3f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float RetryMaximum = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeMoveInput = 0.35f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeReverseInput = -0.35f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeTurnInput = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeMaximumSpeed = 300.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeMaximumYawRate = 8.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ProgressGraceTime = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ProgressWindow = 3.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ProgressMinimumDistance = 100.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float MaximumEscapeTurn = 90.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float EscapeReconsiderInterval = 0.25f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ResumeConfirmationTime = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ResumeAdditionalMargin = 100.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float StoppedSpeed = 10.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float StoppedYawRate = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ChargeTerrainStallGrace = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ChargeTerrainStallTime = 2.0f;
	UPROPERTY(Config, EditAnywhere, Category = "LocalSafety") float ChargeMinimumProgressSpeed = 20.0f;

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

	UPROPERTY(Config, EditAnywhere, Category = "Obstacle", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MaximumEvaluatedObstacles = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "deg"))
	float TerrainProbeYaw = 45.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "cm"))
	float TerrainSideMargin = 400.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "1.0", Units = "cm"))
	float TerrainHalfHeight = 200.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "1.0", Units = "cm"))
	float TerrainMinimumProbeDistance = 1500.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "s"))
	float TerrainVelocityHorizon = 3.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "cm"))
	float TerrainSideTieDistance = 200.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TerrainLimitedMoveInput = 0.35f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
	float TerrainOverlapReverseInput = -0.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "s"))
	float TerrainMinimumTurnTime = 1.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", Units = "s"))
	float TerrainClearConfirmationTime = 0.8f;
};
