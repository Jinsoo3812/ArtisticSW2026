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

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
