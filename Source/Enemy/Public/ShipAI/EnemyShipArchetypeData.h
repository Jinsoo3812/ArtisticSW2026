#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "EnemyShipArchetypeData.generated.h"

class AEnemyShip;
class UEnemyShipSkillModuleData;

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipCannonAimProfile
{
	GENERATED_BODY()

	/** Target speed the cannon can fully lead. Faster movement is deliberately under-predicted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon Aim", meta = (ClampMin = "0.0", Units = "cm/s"))
	float TrackableTargetSpeed = 1000.0f;

	/** Time assigned to every AI cannonball flight; launch velocity is solved from this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon Aim", meta = (ClampMin = "0.05", Units = "s"))
	float ProjectileFlightTime = 3.0f;
};

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FEnemyShipCannonAimProfile CannonAimProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	EEnemyShipSkillSelectionPolicy SelectionPolicy = EEnemyShipSkillSelectionPolicy::HighestPriority;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> SkillModules;

	bool ApplyToShip(AEnemyShip* Ship);
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
