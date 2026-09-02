#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipSkillModuleData.generated.h"

class UGameplayAbility;

UENUM(BlueprintType)
enum class EEnemyShipSkillSelectionPolicy : uint8
{
	HighestPriority,
	WeightedRandom,
	Sequence
};

/** Reusable enemy-ship skill containing its granted GAS ability and AI selection policy. */
UCLASS(BlueprintType)
class ENEMY_API UEnemyShipSkillModuleData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TSubclassOf<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	FGameplayTagContainer RequiredOwnerTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	FGameplayTagContainer BlockedOwnerTags;

	/** Empty means every navigation state is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	TArray<ENavalCombatState> AllowedNavigationStates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	bool bUseOnlyOnce = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EEnemyShipSkillMovementPolicy MovementPolicy = EEnemyShipSkillMovementPolicy::ContinueNavigation;

	FGameplayTag GetAbilityTag() const;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
