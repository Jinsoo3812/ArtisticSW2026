#include "StoryTypes.h"

bool FStoryCounterRequirement::IsSatisfied(const TMap<FGameplayTag, int32>& Counters) const
{
	const int32 ActualValue = Counters.FindRef(CounterTag);
	switch (Comparison)
	{
	case EStoryCounterComparison::Equal:
		return ActualValue == Value;
	case EStoryCounterComparison::NotEqual:
		return ActualValue != Value;
	case EStoryCounterComparison::Less:
		return ActualValue < Value;
	case EStoryCounterComparison::LessOrEqual:
		return ActualValue <= Value;
	case EStoryCounterComparison::Greater:
		return ActualValue > Value;
	case EStoryCounterComparison::GreaterOrEqual:
		return ActualValue >= Value;
	default:
		return false;
	}
}

bool FStoryConditionSet::IsSatisfied(
	const FGameplayTagContainer& Facts,
	const TMap<FGameplayTag, int32>& Counters) const
{
	if (!FactQuery.IsEmpty() && !FactQuery.Matches(Facts))
	{
		return false;
	}

	for (const FStoryCounterRequirement& Requirement : CounterRequirements)
	{
		if (!Requirement.CounterTag.IsValid() || !Requirement.IsSatisfied(Counters))
		{
			return false;
		}
	}
	return true;
}
