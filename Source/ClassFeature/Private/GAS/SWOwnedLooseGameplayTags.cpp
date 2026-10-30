#include "GAS/SWOwnedLooseGameplayTags.h"
#include "AbilitySystemComponent.h"

void FSWOwnedLooseGameplayTags::Set(UAbilitySystemComponent* InASC, FGameplayTag Tag, bool bEnabled)
{
	if (ASC.Get() != InASC)
	{
		Reset();
		ASC = InASC;
	}
	if (!IsValid(InASC) || !Tag.IsValid())
		return;
	if (bEnabled)
	{
		if (!Tags.Contains(Tag))
		{
			Tags.Add(Tag);
			InASC->AddLooseGameplayTag(Tag);
		}
	}
	else if (Tags.Remove(Tag) > 0)
		InASC->RemoveLooseGameplayTag(Tag);
}

void FSWOwnedLooseGameplayTags::Reset()
{
	if (UAbilitySystemComponent* Owner = ASC.Get())
		for (const FGameplayTag& Tag : Tags)
			Owner->RemoveLooseGameplayTag(Tag);
	Tags.Reset();
	ASC.Reset();
}
