#include "GAS/Ability/WeaponGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "BasePlayer.h"
#include "Item/BaseItem.h"

bool UWeaponGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const ABasePlayer* Player = ActorInfo ? Cast<ABasePlayer>(ActorInfo->AvatarActor.Get()) : nullptr;
	const FGameplayAbilitySpec* Spec = ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		? ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle) : nullptr;
	const ABaseItem* Weapon = Spec ? Cast<ABaseItem>(Spec->SourceObject.Get()) : nullptr;
	return IsValid(Weapon) && Player && Player->EquippedItem == Weapon && Weapon->GetWeaponDefinition()
		&& !Player->IsEquipmentTransitioning()
		&& Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

ABaseItem* UWeaponGameplayAbility::GetSourceWeapon() const
{
	return Cast<ABaseItem>(GetSourceObject(CurrentSpecHandle, CurrentActorInfo));
}
