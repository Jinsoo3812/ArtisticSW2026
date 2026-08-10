#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "EnemyShipArchetypeData.generated.h"

class AEnemyShip;
class UEnemyShipAbilitySet;
class UEnemyShipPatternData;

UCLASS(BlueprintType)
class ENEMY_API UEnemyShipArchetypeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spec", meta = (RowType = "/Script/WaterAndShip.ShipStatRow"))
	FDataTableRowHandle SpecRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UEnemyShipPatternData> Pattern;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UEnemyShipAbilitySet> AbilitySet;

	bool ApplyToShip(AEnemyShip* Ship) const;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
