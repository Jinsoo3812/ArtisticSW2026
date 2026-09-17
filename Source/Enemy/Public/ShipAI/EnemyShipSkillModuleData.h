#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipSkillModuleData.generated.h"

class UGameplayAbility;

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipCannonVolleySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon Volley", meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float MinimumElevationDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon Volley", meta = (ClampMin = "1.0", Units = "cm"))
	float ImpactEllipseSemiMajorAxisCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon Volley", meta = (ClampMin = "1.0", Units = "cm"))
	float ImpactEllipseSemiMinorAxisCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon Volley", meta = (ClampMin = "0.0"))
	float AttackerFacingHalfWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cannon Volley", meta = (ClampMin = "0.0"))
	float AttackerOppositeHalfWeight = 1.0f;
};

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Cannon Volley", meta = (ShowOnlyInnerProperties))
	FEnemyShipCannonVolleySettings CannonVolleySettings;

	FGameplayTag GetAbilityTag() const;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
