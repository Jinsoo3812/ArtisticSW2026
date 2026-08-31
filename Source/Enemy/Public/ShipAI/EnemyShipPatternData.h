#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipPatternData.generated.h"

class UGameplayAbility;
class UEnemyShipSkillModuleData;

UENUM(BlueprintType)
enum class EEnemyShipPatternSelectionPolicy : uint8
{
	HighestPriority,
	WeightedRandom,
	Sequence
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipSkillRule
{
	GENERATED_BODY()

	/** Stable identifier used across composed modules and Behavior Tree commits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FName RuleId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FGameplayTag AbilityTag;

	/** Optional authoring cross-check. Activation still uses AbilityTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TSubclassOf<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumInterval = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	FGameplayTagContainer RequiredOwnerTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	FGameplayTagContainer BlockedOwnerTags;

	/** Empty means every navigation state is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	TArray<ENavalCombatState> AllowedNavigationStates;

	/** 0 aims at the current target location; 1 leads a constant-velocity target to the solved impact time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetPredictionStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	bool bUseOnlyOnce = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EEnemyShipSkillMovementPolicy MovementPolicy = EEnemyShipSkillMovementPolicy::ContinueNavigation;
};

UCLASS(BlueprintType)
class ENEMY_API UEnemyShipPatternData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FEnemyShipNavigationProfile NavigationProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	EEnemyShipPatternSelectionPolicy SelectionPolicy = EEnemyShipPatternSelectionPolicy::HighestPriority;

	/** Optional skill modules composed with the Enemy Ship's always-on Core modules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills", meta = (TitleProperty = "ModuleId"))
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> SkillModules;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
