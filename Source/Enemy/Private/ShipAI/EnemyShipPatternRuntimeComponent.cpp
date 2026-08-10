#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipPatternData.h"

UEnemyShipPatternRuntimeComponent::UEnemyShipPatternRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	RandomStream.Initialize(0);
}

void UEnemyShipPatternRuntimeComponent::SetPattern(UEnemyShipPatternData* InPattern)
{
	Pattern = InPattern;
	ResetRuntimeState(0);
}

bool UEnemyShipPatternRuntimeComponent::SelectAbility(
	AActor* TargetActor,
	FEnemyShipAbilitySelection& OutSelection)
{
	const UWorld* World = GetWorld();
	return SelectAbilityAtTime(TargetActor, World ? World->GetTimeSeconds() : 0.0, OutSelection);
}

bool UEnemyShipPatternRuntimeComponent::SelectAbilityAtTime(
	AActor* TargetActor,
	double CurrentTimeSeconds,
	FEnemyShipAbilitySelection& OutSelection)
{
	OutSelection = FEnemyShipAbilitySelection();
	PendingRuleIndex = INDEX_NONE;
	PendingSelectionTime = CurrentTimeSeconds;

	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Pattern || !Ship || !Ship->HasAuthority() || !IsValid(TargetActor))
	{
		return false;
	}

	float HealthRatio = 1.0f;
	FGameplayTagContainer OwnerTags;
	if (const UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
	{
		const float Health = ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
		const float MaxHealth = ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute());
		HealthRatio = MaxHealth > KINDA_SMALL_NUMBER ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f;
		ASC->GetOwnedGameplayTags(OwnerTags);
	}

	TArray<int32> EligibleIndices;
	for (int32 RuleIndex = 0; RuleIndex < Pattern->SkillRules.Num(); ++RuleIndex)
	{
		if (IsRuleEligible(RuleIndex, TargetActor, CurrentTimeSeconds, HealthRatio, OwnerTags))
		{
			EligibleIndices.Add(RuleIndex);
		}
	}

	const int32 SelectedIndex = SelectEligibleIndex(EligibleIndices);
	if (!Pattern->SkillRules.IsValidIndex(SelectedIndex))
	{
		return false;
	}

	const FEnemyShipSkillRule& Rule = Pattern->SkillRules[SelectedIndex];
	OutSelection.AbilityTag = Rule.AbilityTag;
	OutSelection.MovementPolicy = Rule.MovementPolicy;
	OutSelection.RuleIndex = SelectedIndex;
	PendingRuleIndex = SelectedIndex;
	return true;
}

bool UEnemyShipPatternRuntimeComponent::CommitSelection(const FEnemyShipAbilitySelection& Selection)
{
	if (!Pattern || Selection.RuleIndex != PendingRuleIndex
		|| !Pattern->SkillRules.IsValidIndex(Selection.RuleIndex)
		|| Pattern->SkillRules[Selection.RuleIndex].AbilityTag != Selection.AbilityTag)
	{
		return false;
	}

	LastCommittedTimes.FindOrAdd(Selection.RuleIndex) = PendingSelectionTime;
	if (Pattern->SkillRules[Selection.RuleIndex].bUseOnlyOnce)
	{
		ConsumedOneShotRules.Add(Selection.RuleIndex);
	}
	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::Sequence && Pattern->SkillRules.Num() > 0)
	{
		SequenceCursor = (Selection.RuleIndex + 1) % Pattern->SkillRules.Num();
	}
	PendingRuleIndex = INDEX_NONE;
	return true;
}

void UEnemyShipPatternRuntimeComponent::ResetRuntimeState(int32 RandomSeed)
{
	LastCommittedTimes.Reset();
	ConsumedOneShotRules.Reset();
	RandomStream.Initialize(RandomSeed);
	SequenceCursor = 0;
	PendingSelectionTime = 0.0;
	PendingRuleIndex = INDEX_NONE;
}

double UEnemyShipPatternRuntimeComponent::GetLastCommittedTime(int32 RuleIndex) const
{
	if (const double* Time = LastCommittedTimes.Find(RuleIndex))
	{
		return *Time;
	}
	return -1.0;
}

bool UEnemyShipPatternRuntimeComponent::IsRuleEligible(
	int32 RuleIndex,
	AActor* TargetActor,
	double CurrentTimeSeconds,
	float OwnerHealthRatio,
	const FGameplayTagContainer& OwnerTags) const
{
	if (!Pattern || !Pattern->SkillRules.IsValidIndex(RuleIndex) || ConsumedOneShotRules.Contains(RuleIndex))
	{
		return false;
	}

	const FEnemyShipSkillRule& Rule = Pattern->SkillRules[RuleIndex];
	if (!Rule.AbilityTag.IsValid() || !IsValid(TargetActor))
	{
		return false;
	}
	if (OwnerHealthRatio < Rule.MinimumHealthRatio || OwnerHealthRatio > Rule.MaximumHealthRatio)
	{
		return false;
	}
	if (!OwnerTags.HasAll(Rule.RequiredOwnerTags) || OwnerTags.HasAny(Rule.BlockedOwnerTags))
	{
		return false;
	}

	const float Distance = FVector::Dist2D(GetOwner()->GetActorLocation(), TargetActor->GetActorLocation());
	if (Distance < Rule.MinimumDistance || Distance > Rule.MaximumDistance)
	{
		return false;
	}
	if (const double* LastTime = LastCommittedTimes.Find(RuleIndex))
	{
		if (CurrentTimeSeconds - *LastTime < Rule.MinimumInterval)
		{
			return false;
		}
	}
	return true;
}

int32 UEnemyShipPatternRuntimeComponent::SelectEligibleIndex(const TArray<int32>& EligibleIndices)
{
	if (!Pattern || EligibleIndices.IsEmpty())
	{
		return INDEX_NONE;
	}

	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::Sequence)
	{
		for (int32 Offset = 0; Offset < Pattern->SkillRules.Num(); ++Offset)
		{
			const int32 Candidate = (SequenceCursor + Offset) % Pattern->SkillRules.Num();
			if (EligibleIndices.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return INDEX_NONE;
	}

	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::WeightedRandom)
	{
		float TotalWeight = 0.0f;
		for (const int32 Index : EligibleIndices)
		{
			TotalWeight += FMath::Max(0.0f, Pattern->SkillRules[Index].Weight);
		}
		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		float Roll = RandomStream.FRandRange(0.0f, TotalWeight);
		for (const int32 Index : EligibleIndices)
		{
			Roll -= FMath::Max(0.0f, Pattern->SkillRules[Index].Weight);
			if (Roll <= 0.0f)
			{
				return Index;
			}
		}
		return EligibleIndices.Last();
	}

	int32 BestIndex = EligibleIndices[0];
	for (const int32 Index : EligibleIndices)
	{
		if (Pattern->SkillRules[Index].Priority > Pattern->SkillRules[BestIndex].Priority)
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}
