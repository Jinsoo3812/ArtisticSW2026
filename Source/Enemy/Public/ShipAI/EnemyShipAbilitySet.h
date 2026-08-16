#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyShipAbilitySet.generated.h"

class UGameplayAbility;

UCLASS(BlueprintType)
class ENEMY_API UEnemyShipAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> Abilities;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
