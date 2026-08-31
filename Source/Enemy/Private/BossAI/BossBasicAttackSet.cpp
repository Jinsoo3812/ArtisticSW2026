#include "BossAI/BossBasicAttackSet.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Misc/DataValidation.h"

const FBossBasicAttackEntry* UBossBasicAttackSet::FindAttack(const FName AttackId) const
{
	return Attacks.FindByPredicate([AttackId](const FBossBasicAttackEntry& Entry)
	{
		return Entry.AttackId == AttackId;
	});
}

const FBossBasicAttackEntry* UBossBasicAttackSet::SelectAttack(
	const UAbilitySystemComponent* AbilitySystem,
	const FName PreviousAttackId,
	const float RandomFraction) const
{
	TArray<const FBossBasicAttackEntry*> EligibleAttacks;
	float TotalWeight = 0.0f;
	for (const FBossBasicAttackEntry& Entry : Attacks)
	{
		const bool bOnCooldown = AbilitySystem
			&& Entry.IndividualCooldownTag.IsValid()
			&& AbilitySystem->HasMatchingGameplayTag(Entry.IndividualCooldownTag);
		if (!Entry.AttackMontage || Entry.SelectionWeight <= 0.0f || bOnCooldown)
		{
			continue;
		}

		EligibleAttacks.Add(&Entry);
	}

	if (bAvoidImmediateRepeat && EligibleAttacks.Num() > 1)
	{
		EligibleAttacks.RemoveAll([PreviousAttackId](const FBossBasicAttackEntry* Entry)
		{
			return Entry && Entry->AttackId == PreviousAttackId;
		});
	}

	for (const FBossBasicAttackEntry* Entry : EligibleAttacks)
	{
		TotalWeight += Entry ? FMath::Max(0.0f, Entry->SelectionWeight) : 0.0f;
	}
	if (EligibleAttacks.IsEmpty() || TotalWeight <= 0.0f)
	{
		return nullptr;
	}

	float Roll = FMath::Clamp(RandomFraction, 0.0f, 1.0f - UE_KINDA_SMALL_NUMBER) * TotalWeight;
	for (const FBossBasicAttackEntry* Entry : EligibleAttacks)
	{
		Roll -= FMath::Max(0.0f, Entry->SelectionWeight);
		if (Roll < 0.0f)
		{
			return Entry;
		}
	}
	return EligibleAttacks.Last();
}

EDataValidationResult UBossBasicAttackSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FName> SeenIds;
	if (Attacks.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("Boss basic attack set requires at least one attack.")));
		Result = EDataValidationResult::Invalid;
	}
	for (const FBossBasicAttackEntry& Entry : Attacks)
	{
		if (Entry.AttackId.IsNone() || SeenIds.Contains(Entry.AttackId))
		{
			Context.AddError(FText::FromString(TEXT("Boss basic attacks require unique, non-empty AttackIds.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenIds.Add(Entry.AttackId);

		if (!Entry.AttackMontage || Entry.SelectionWeight <= 0.0f)
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Boss basic attack '%s' requires a Montage and positive weight."), *Entry.AttackId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.IndividualCooldownTag.IsValid() != (Entry.IndividualCooldownDuration > 0.0f))
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Boss basic attack '%s' cooldown tag and duration must be configured together."), *Entry.AttackId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.bUseTimedHitScanWindow
			&& (Entry.TimedHitScanStartNormalized < 0.0f
				|| Entry.TimedHitScanStartNormalized >= 1.0f
				|| Entry.TimedHitScanDurationNormalized <= 0.0f
				|| Entry.TimedHitScanStartNormalized + Entry.TimedHitScanDurationNormalized > 1.0f))
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Boss basic attack '%s' has an invalid normalized timed hit-scan window."), *Entry.AttackId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
