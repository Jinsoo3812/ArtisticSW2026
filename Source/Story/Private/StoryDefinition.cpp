#include "StoryDefinition.h"

const FStoryStateRule* UStoryDefinition::FindStateRule(FGameplayTag StateTag) const
{
	return StateRules.FindByPredicate([StateTag](const FStoryStateRule& Rule)
	{
		return Rule.StateTag.MatchesTagExact(StateTag);
	});
}

bool UStoryDefinition::ValidateDefinition(TArray<FText>& OutErrors) const
{
	OutErrors.Reset();
	TSet<FGameplayTag> StateTags;
	TSet<FGameplayTag> CounterTags;

	for (const FStoryCounterValue& Counter : InitialCounters)
	{
		if (!Counter.CounterTag.IsValid())
		{
			OutErrors.Add(NSLOCTEXT("Story", "InvalidInitialCounter", "An initial story counter has an invalid tag."));
		}
		else if (CounterTags.Contains(Counter.CounterTag))
		{
			OutErrors.Add(FText::Format(
				NSLOCTEXT("Story", "DuplicateInitialCounter", "Initial counter is duplicated: {0}"),
				FText::FromString(Counter.CounterTag.ToString())));
		}
		CounterTags.Add(Counter.CounterTag);
	}

	for (const FStoryStateRule& Rule : StateRules)
	{
		if (!Rule.StateTag.IsValid())
		{
			OutErrors.Add(NSLOCTEXT("Story", "InvalidStateTag", "A story state rule has an invalid StateTag."));
			continue;
		}
		if (StateTags.Contains(Rule.StateTag))
		{
			OutErrors.Add(FText::Format(
				NSLOCTEXT("Story", "DuplicateStateTag", "Story state is duplicated: {0}"),
				FText::FromString(Rule.StateTag.ToString())));
		}
		StateTags.Add(Rule.StateTag);

		TSet<FName> ActionIds;
		for (const FStoryCounterRequirement& Requirement : Rule.ActivationCondition.CounterRequirements)
		{
			if (!Requirement.CounterTag.IsValid())
			{
				OutErrors.Add(FText::Format(
					NSLOCTEXT("Story", "InvalidCounterRequirement", "State {0} has an invalid counter requirement."),
					FText::FromString(Rule.StateTag.ToString())));
			}
		}
		for (const FStoryActionSpec& Action : Rule.ActivationActions)
		{
			if (Action.ActionId.IsNone() || !Action.ActionType.IsValid())
			{
				OutErrors.Add(FText::Format(
					NSLOCTEXT("Story", "InvalidAction", "State {0} has an action without a valid ActionId or ActionType."),
					FText::FromString(Rule.StateTag.ToString())));
				continue;
			}
			if (ActionIds.Contains(Action.ActionId))
			{
				OutErrors.Add(FText::Format(
					NSLOCTEXT("Story", "DuplicateActionId", "State {0} has duplicate action id {1}."),
					FText::FromString(Rule.StateTag.ToString()),
					FText::FromName(Action.ActionId)));
			}
			ActionIds.Add(Action.ActionId);
		}
	}

	return OutErrors.IsEmpty();
}
