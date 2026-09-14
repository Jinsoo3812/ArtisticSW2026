#include "StatusEffectLibrary.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "BaseGameplayTags.h"
#include "StatusReceiver.h"
#include "GAS/SWCombatEffectContextLibrary.h"

FGameplayTag UStatusEffectLibrary::CanonicalStatusTag(FGameplayTag Tag)
{
	if (Tag == State_Poisoned) return State_Status_Poison;
	if (Tag == State_Debuff_Slow) return State_Status_Slow;
	if (Tag == State_Debuff_WaterBomb) return State_Status_WaterBomb;
	if (Tag == State_CrowdControl_Knockback) return State_Status_Knockback;
	return Tag;
}

FGameplayTag UStatusEffectLibrary::ResolveStatusTag(const FGameplayEffectSpec& Spec)
{
	FGameplayTagContainer Tags;
	Spec.GetAllGrantedTags(Tags);
	FGameplayTag Result;
	for (FGameplayTag Tag : Tags)
	{
		Tag = CanonicalStatusTag(Tag);
		if (!Tag.MatchesTag(State_Status) || Tag == State_Status) continue;
		if (Result.IsValid() && Result != Tag) return {};
		Result = Tag;
	}
	return Result;
}

bool UStatusEffectLibrary::CanApplyStatus(UAbilitySystemComponent* TargetASC,
	const FGameplayEffectSpec& Spec, FGameplayTag StatusTag)
{
	StatusTag = CanonicalStatusTag(StatusTag);
	if (!TargetASC || !Spec.Def || !TargetASC->IsOwnerActorAuthoritative()
		|| !StatusTag.MatchesTag(State_Status) || StatusTag == State_Status
		|| TargetASC->HasMatchingGameplayTag(State_Dead)) return false;
	FGameplayTagContainer OwnedTags;
	TargetASC->GetOwnedGameplayTags(OwnedTags);
	const FGameplayTag SpecificImmunity = FGameplayTag::RequestGameplayTag(
		FName(*StatusTag.ToString().Replace(TEXT("State.Status."), TEXT("Immunity.Status."))), false);
	if (OwnedTags.HasTagExact(Immunity_Status) || (SpecificImmunity.IsValid() && OwnedTags.HasTag(SpecificImmunity))) return false;
	for (FGameplayTag Tag : OwnedTags)
		if (CanonicalStatusTag(Tag) == StatusTag) return false;
	AActor* Avatar = TargetASC->GetAvatarActor();
	if (!Avatar) return false;
	if (Avatar->Implements<UStatusReceiver>()) return IStatusReceiver::Execute_CanReceiveStatus(Avatar, StatusTag);
	for (UActorComponent* Component : Avatar->GetComponents())
		if (Component && Component->Implements<UStatusReceiver>()
			&& IStatusReceiver::Execute_CanReceiveStatus(Component, StatusTag)) return true;
	return false;
}

FActiveGameplayEffectHandle UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(
	UAbilitySystemComponent* TargetASC,
	const FGameplayEffectSpecHandle& EffectSpecHandle,
	FGameplayTag RefreshGrantedTag)
{
	if (!TargetASC || !EffectSpecHandle.IsValid() || !EffectSpecHandle.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpec EffectSpec(*EffectSpecHandle.Data.Get());
	EffectSpec.DuplicateEffectContext();
	USWCombatEffectContextLibrary::SetDamageDeliveryType(
		EffectSpec.GetContext(), ESWDamageDeliveryType::StatusTick);
	if (!EffectSpec.Def)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayTag StatusTag = ResolveStatusTag(EffectSpec);
	if (!StatusTag.IsValid()) StatusTag = CanonicalStatusTag(RefreshGrantedTag);
	if (!CanApplyStatus(TargetASC, EffectSpec, StatusTag)) return {};
	EffectSpec.DynamicGrantedTags.AddTag(StatusTag);

	return TargetASC->ApplyGameplayEffectSpecToSelf(EffectSpec);
}
