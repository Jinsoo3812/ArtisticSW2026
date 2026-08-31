#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "EnemyShipSkillModuleData.generated.h"

class UEnemyShipAbilitySet;

/** Reusable plug-in containing both granted GAS abilities and their scheduling rules. */
UCLASS(BlueprintType)
class ENEMY_API UEnemyShipSkillModuleData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	FName ModuleId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TObjectPtr<UEnemyShipAbilitySet> AbilitySet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (TitleProperty = "RuleId"))
	TArray<FEnemyShipSkillRule> SkillRules;

	/**
	 * Absolute launch-speed ceiling used by CannonVolley rules in this module.
	 * If no aimable ballistic solution exists at or below this speed, the skill cannot fire.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon", meta = (ClampMin = "1.0", DisplayName = "Maximum Cannonball Speed (cm/s)"))
	float MaximumCannonballSpeed = 5000.0f;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
