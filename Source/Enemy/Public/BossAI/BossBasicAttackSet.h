#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "BossBasicAttackSet.generated.h"

class UAbilitySystemComponent;
class UAnimMontage;

UENUM(BlueprintType)
enum class EBossBasicAttackType : uint8
{
	Short UMETA(DisplayName = "Short"),
	Combo UMETA(DisplayName = "Combo")
};

/** One visual variation of the Boss's current weapon basic attack. */
USTRUCT(BlueprintType)
struct ENEMY_API FBossBasicAttackEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	FName AttackId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	EBossBasicAttackType AttackType = EBossBasicAttackType::Short;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.001"))
	float AttackMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.0f;

	/** Fallback for source animations that do not author ANS_HitScanWindow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit Scan")
	bool bUseTimedHitScanWindow = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit Scan",
		meta = (EditCondition = "bUseTimedHitScanWindow", ClampMin = "0.0", ClampMax = "1.0"))
	float TimedHitScanStartNormalized = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit Scan",
		meta = (EditCondition = "bUseTimedHitScanWindow", ClampMin = "0.01", ClampMax = "1.0"))
	float TimedHitScanDurationNormalized = 0.25f;

	/** Empty for normal variations. Combo attacks use an independent cooldown tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown", meta = (Categories = "Cooldown.Boss.BasicAttack"))
	FGameplayTag IndividualCooldownTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown", meta = (ClampMin = "0.0", Units = "s"))
	float IndividualCooldownDuration = 0.0f;
};

/**
 * Boss-only moveset data. Weapon reach, damage effect, impact cue, and trace
 * still come from the equipped WeaponDefinition; this asset only varies the
 * montage and selection/cooldown policy.
 */
UCLASS(BlueprintType)
class ENEMY_API UBossBasicAttackSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss Basic Attack")
	TArray<FBossBasicAttackEntry> Attacks;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss Basic Attack|Selection")
	bool bAvoidImmediateRepeat = true;

	const FBossBasicAttackEntry* FindAttack(FName AttackId) const;
	const FBossBasicAttackEntry* SelectAttack(
		const UAbilitySystemComponent* AbilitySystem,
		FName PreviousAttackId,
		float RandomFraction) const;

	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
