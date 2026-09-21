#include "WeaponInputAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Item/BaseItem.h"

void UWeaponInputAbilitySystemComponent::InputTagPressed(FGameplayTag InputTag) { RouteInput(InputTag, true); }
void UWeaponInputAbilitySystemComponent::InputTagReleased(FGameplayTag InputTag) { RouteInput(InputTag, false); }

void UWeaponInputAbilitySystemComponent::RouteInput(FGameplayTag InputTag, bool bPressed)
{
	if (!InputTag.IsValid()) return;
	ABILITYLIST_SCOPE_LOCK();
	// Equipped item bindings take precedence over character defaults on the same input.
	const bool bHasItemBinding = GetActivatableAbilities().ContainsByPredicate([InputTag](const FGameplayAbilitySpec& Spec)
	{
		return Cast<ABaseItem>(Spec.SourceObject.Get()) && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag);
	});
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)) continue;
		if (bPressed && bHasItemBinding && !Cast<ABaseItem>(Spec.SourceObject.Get())) continue;
		Spec.InputPressed = bPressed;
		if (Spec.IsActive())
		{
			if (Spec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				if (bPressed) ServerSetInputPressed(Spec.Handle);
				else ServerSetInputReleased(Spec.Handle);
			}
			if (bPressed) AbilitySpecInputPressed(Spec);
			else AbilitySpecInputReleased(Spec);
			// WaitInputRelease/Pressed uses the active instance's prediction key.
			const TArray<UGameplayAbility*> Instances = Spec.GetAbilityInstances();
			const FPredictionKey Key = Instances.IsEmpty() ? FPredictionKey()
				: Instances.Last()->GetCurrentActivationInfo().GetActivationPredictionKey();
			InvokeReplicatedEvent(bPressed ? EAbilityGenericReplicatedEvent::InputPressed
				: EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, Key);
		}
		else if (bPressed) TryActivateAbility(Spec.Handle);
	}
}
