#include "GAS/Ability/Boss/BossGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "GASCombatLibrary.h"

UBossAbilityCooldownEffect::UBossAbilityCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
}

UBossAbilityStateEffect::UBossAbilityStateEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
}

UBossGameplayAbility::UBossGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationOwnedTags.AddTag(State_Boss_Busy);
	ActivationOwnedTags.AddTag(State_Attacking);
	ActivationBlockedTags.AddTag(State_Boss_Busy);
	ActivationBlockedTags.AddTag(State_Damaged);
	ActivationBlockedTags.AddTag(State_Dead);
}

const FGameplayTagContainer* UBossGameplayAbility::GetCooldownTags() const
{
	return &NativeCooldownTags;
}

void UBossGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || NativeCooldownTags.IsEmpty() || CooldownDuration <= 0.0f)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UBossAbilityCooldownEffect::StaticClass(),
		GetAbilityLevel(Handle, ActorInfo),
		Context);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return;
	}
	Spec.Data->SetDuration(CooldownDuration, true);
	Spec.Data->DynamicGrantedTags.AppendTags(NativeCooldownTags);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UBossGameplayAbility::SetBossAbilityTags(FGameplayTag AbilityTag, FGameplayTag InCooldownTag)
{
	FGameplayTagContainer BossAbilityTags;
	if (AbilityTag.IsValid())
	{
		BossAbilityTags.AddTag(AbilityTag);
	}
	BossAbilityTags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(BossAbilityTags);

	CooldownTag = InCooldownTag;
	NativeCooldownTags.Reset();
	if (CooldownTag.IsValid())
	{
		NativeCooldownTags.AddTag(CooldownTag);
	}
}

AShipBossEnemy* UBossGameplayAbility::GetBossAvatar() const
{
	return Cast<AShipBossEnemy>(GetAvatarActorFromActorInfo());
}

AActor* UBossGameplayAbility::GetBossTarget() const
{
	const AShipBossEnemy* Boss = GetBossAvatar();
	return Boss ? Boss->GetBossCombatTarget() : nullptr;
}

bool UBossGameplayAbility::ApplyDamageToTarget(
	AActor* Target,
	TSubclassOf<UGameplayEffect> DamageEffectClass,
	float Damage) const
{
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!SourceASC || !TargetASC || !Boss || !DamageEffectClass)
	{
		return false;
	}

	const FGameplayEffectSpecHandle Spec = UGASCombatLibrary::MakeDamageEffectSpec(
		SourceASC,
		DamageEffectClass,
		Damage,
		Boss,
		Boss);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return false;
	}
	TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	return true;
}

FActiveGameplayEffectHandle UBossGameplayAbility::ApplyTimedStateTag(
	UAbilitySystemComponent& TargetASC,
	FGameplayTag StateTag,
	float Duration) const
{
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!SourceASC || !Boss || !StateTag.IsValid() || Duration <= 0.0f)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Boss, Boss);
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UBossAbilityStateEffect::StaticClass(),
		1.0f,
		Context);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	Spec.Data->SetDuration(Duration, true);
	Spec.Data->DynamicGrantedTags.AddTag(StateTag);
	return TargetASC.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
