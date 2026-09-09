#pragma once

#include "CoreMinimal.h"
#include "EnemyPerceptionSettings.generated.h"

/**
 * Per-enemy AI Perception tuning authored on an Enemy Blueprint.
 *
 * Damage Sense has no spatial radius in Unreal Engine. DamageMaxAge controls
 * how long a reported damage stimulus remains relevant to the listener.
 */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyPerceptionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sight", meta = (ClampMin = "0.0", Units = "cm"))
	float SightRadius = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sight", meta = (ClampMin = "0.0", Units = "cm"))
	float LoseSightRadius = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sight", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float PeripheralVisionDegrees = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sight", meta = (ClampMin = "0.0", Units = "s"))
	float SightMaxAge = 3.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sight", meta = (ClampMin = "0.0", Units = "cm"))
	float AutoSuccessRangeFromLastSeenLocation = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hearing", meta = (ClampMin = "0.0", Units = "cm"))
	float HearingRange = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hearing", meta = (ClampMin = "0.0", Units = "s"))
	float HearingMaxAge = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0", Units = "s"))
	float DamageMaxAge = 5.0f;
};
