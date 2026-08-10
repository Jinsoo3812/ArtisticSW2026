#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"

#include "AbilitySystemComponent.h"

UEnemyShipAbilityCooldownEffect::UEnemyShipAbilityCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
}

UEnemyShipGameplayAbility::UEnemyShipGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

const FGameplayTagContainer* UEnemyShipGameplayAbility::GetCooldownTags() const
{
	return &NativeCooldownTags;
}

void UEnemyShipGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || NativeCooldownTags.IsEmpty() || CooldownDurationSeconds <= 0.0f)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(
		UEnemyShipAbilityCooldownEffect::StaticClass(), GetAbilityLevel(Handle, ActorInfo), Context);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return;
	}

	SpecHandle.Data->SetDuration(CooldownDurationSeconds, true);
	SpecHandle.Data->DynamicGrantedTags.AppendTags(NativeCooldownTags);
	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

void UEnemyShipGameplayAbility::SetNativeAbilityAndCooldownTags(
	FGameplayTag AbilityTag,
	FGameplayTag InCooldownTag)
{
	FGameplayTagContainer NativeAbilityTags;
	NativeAbilityTags.AddTag(AbilityTag);
	SetAssetTags(NativeAbilityTags);

	CooldownTag = InCooldownTag;
	NativeCooldownTags.Reset();
	NativeCooldownTags.AddTag(InCooldownTag);
}
