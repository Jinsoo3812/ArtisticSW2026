#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyShipWeakeningData.generated.h"

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipWeakeningPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Weakening")
	float ShipHealthRatio = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Weakening")
	float StrengthMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Weakening")
	float MoveSpeedMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Weakening")
	float AttackSpeedMultiplier = 1.f;
};

UCLASS(BlueprintType)
class ENEMY_API UEnemyShipWeakeningData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Weakening")
	TArray<FEnemyShipWeakeningPoint> Points;

	bool Evaluate(float HealthRatio, FEnemyShipWeakeningPoint& OutPoint) const;
};
