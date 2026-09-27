#include "EnemyBalanceData.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool FEnemyCombatBalanceRow::IsValid() const
{
	return FMath::IsFinite(AttackInterval) && AttackInterval >= 0.f
		&& FMath::IsFinite(InitialAttackDelay) && InitialAttackDelay >= 0.f && MaxSimultaneousMeleeAttackers >= 0;
}

bool FEnemyBaseStatsRow::IsValid() const
{
	return Tier > 0 && StaticEnum<EEnemyBalanceType>()->IsValidEnumValue(static_cast<int64>(EnemyType))
		&& FMath::IsFinite(MaxHealth) && MaxHealth > 0.f
		&& FMath::IsFinite(Strength) && Strength >= 0.f
		&& FMath::IsFinite(MoveSpeedMultiplier) && MoveSpeedMultiplier >= .1f && MoveSpeedMultiplier <= 3.f
		&& FMath::IsFinite(AttackSpeedMultiplier) && AttackSpeedMultiplier >= .1f && AttackSpeedMultiplier <= 3.f;
}

#if WITH_EDITOR
EDataValidationResult FEnemyBaseStatsRow::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = IsValid();
	if (!CombatSettings.IsNull())
	{
		const auto* Combat = CombatSettings.GetRow<FEnemyCombatBalanceRow>(TEXT("Enemy balance validation"));
		bValid &= Combat && Combat->IsValid();
	}
	if (!bValid)
	{
		Context.AddError(FText::FromString(TEXT("Invalid enemy stats or combat settings row.")));
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
