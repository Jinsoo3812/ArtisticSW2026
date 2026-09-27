#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EnemyBalanceData.generated.h"

UENUM(BlueprintType)
enum class EEnemyBalanceType : uint8
{
	GroundMelee, GroundRanged, DeckMelee, DeckRanged, BossSummonRanged, Boss
};

/** Cadence is AI configuration, not a GAS attack-speed attribute. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyCombatBalanceRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Zero preserves the existing ability/AI cooldown. Start-to-start minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float AttackInterval = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float InitialAttackDelay = 0.f;
	/** Zero disables coordination. Counts active melee attacks against the same target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	int32 MaxSimultaneousMeleeAttackers = 0;
	bool IsValid() const;
};

/** Designer-selected row; Tier and EnemyType are labels, never a runtime lookup formula. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyBaseStatsRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Tier = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EEnemyBalanceType EnemyType = EEnemyBalanceType::GroundMelee;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.01"))
	float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	float Strength = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", ClampMax = "3"))
	float MoveSpeedMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", ClampMax = "3"))
	float AttackSpeedMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (RowType = "/Script/Enemy.EnemyCombatBalanceRow"))
	FDataTableRowHandle CombatSettings;

	bool IsValid() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Encounter authoring preset. Counts describe placement; summons are consumed at runtime. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyEncounterBalanceRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 GroundMeleeCount = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 GroundRangedCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DeckMeleeCount = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DeckRangedCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SummonCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SummonAliveLimit = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> SummonHealthFractions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (RowType = "/Script/Enemy.EnemyBaseStatsRow"))
	FDataTableRowHandle SummonStats;
	/** Damage/telegraph targets for authoring the boss's actual montage and ability assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StrongAttackDamage = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MajorAttackDamage = 24.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MajorAttackTelegraphSeconds = 1.2f;
};
