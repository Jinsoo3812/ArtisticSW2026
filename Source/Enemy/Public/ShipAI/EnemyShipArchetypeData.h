#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "EnemyShipArchetypeData.generated.h"

class AEnemyShip;
class UEnemyShipSkillModuleData;

UCLASS(BlueprintType)
class ENEMY_API UEnemyShipArchetypeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spec", meta = (RowType = "/Script/WaterAndShip.ShipStatRow"))
	FDataTableRowHandle SpecRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FEnemyShipNavigationProfile NavigationProfile;

	/** Preferred radial separation between ships in the same squad. The squad uses the members' average value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float OrbitDistanceSpacing = 3000.0f;

	/** Cannon cooldown multiplier at zero health; interpolates linearly to 1 at full health. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1.0"))
	float ZeroHealthCannonCooldownMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	EEnemyShipSkillSelectionPolicy SelectionPolicy = EEnemyShipSkillSelectionPolicy::HighestPriority;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> SkillModules;

	bool ApplyToShip(AEnemyShip* Ship);
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
