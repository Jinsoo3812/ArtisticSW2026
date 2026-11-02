#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
class UAbilitySystemComponent;

/** A single owner contributes one count per tag, and releases only those counts.
 * Explicit Reset belongs in EndAbility/EndPlay; no UObject access from a destructor.
 */
struct CLASSFEATURE_API FSWOwnedLooseGameplayTags
{
	void Set(UAbilitySystemComponent* InASC, FGameplayTag Tag, bool bEnabled);
	void Reset();

  private:
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	TSet<FGameplayTag> Tags;
};
