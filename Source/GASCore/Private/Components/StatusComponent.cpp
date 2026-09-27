#include "Components/StatusComponent.h"
#include "Abilities/StunGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "StatusEffectLibrary.h"
#include "GameFramework/Character.h"

UStatusComponent::UStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SupportedStatuses.AddTag(State_Status_Stun);
	SupportedStatuses.AddTag(State_Status_Poison);
	SupportedStatuses.AddTag(State_Status_Burn);
	SupportedStatuses.AddTag(State_Status_Slow);
	SupportedStatuses.AddTag(State_Status_WaterBomb);
	SupportedStatuses.AddTag(State_Status_Knockback);
	StunAbilityClass = UStunGameplayAbility::StaticClass();
}

void UStatusComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InASC)
{
	if (ASC == InASC) return;
	Uninitialize();
	ASC = InASC;
	if (!ASC) return;
	if (ASC->IsOwnerActorAuthoritative())
	{
		ASC->AddLooseGameplayTag(Capability_Status_Receive, 1, EGameplayTagReplicationState::TagOnly);
		if (StunAbilityClass)
			StunAbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(StunAbilityClass, 1));
	}
	StunDelegate = ASC->RegisterGameplayTagEvent(State_Status_Stun).AddUObject(this, &UStatusComponent::HandleStunChanged);
	DeathDelegate = ASC->RegisterGameplayTagEvent(State_Dead).AddUObject(this, &UStatusComponent::HandleDeathChanged);
	HandleStunChanged(State_Status_Stun, ASC->GetTagCount(State_Status_Stun));
}

bool UStatusComponent::CanReceiveStatus_Implementation(FGameplayTag StatusTag) const
{
	if (StatusTag == State_Status_Stun && (!StunAbilityClass || !Cast<ACharacter>(GetOwner()))) return false;
	return ASC && ASC->GetAvatarActor() == GetOwner() && SupportedStatuses.HasTagExact(StatusTag)
		&& !StatusTag.MatchesAny(ImmuneStatuses) && !ASC->HasMatchingGameplayTag(State_Dead)
		&& ASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetHealthAttribute())
		&& ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) > 0.f;
}

FActiveGameplayEffectHandle UStatusComponent::ApplyStatus(TSubclassOf<UGameplayEffect> EffectClass,
	UAbilitySystemComponent* SourceASC, const FGameplayEffectContextHandle& Context, float Level)
{
	if (!ASC || !EffectClass || !ASC->IsOwnerActorAuthoritative()) return {};
	UAbilitySystemComponent* Source = SourceASC ? SourceASC : ASC.Get();
	const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(EffectClass, Level,
		Context.IsValid() ? Context : Source->MakeEffectContext());
	return UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(ASC, Spec, FGameplayTag());
}

bool UStatusComponent::HasStatus(FGameplayTag StatusTag) const
{
	return ASC && ASC->HasMatchingGameplayTag(UStatusEffectLibrary::CanonicalStatusTag(StatusTag));
}

void UStatusComponent::HandleStunChanged(FGameplayTag Tag, int32 Count)
{
	if (ASC && Count > 0 && ASC->IsOwnerActorAuthoritative() && !ASC->HasMatchingGameplayTag(State_Dead))
		ASC->TryActivateAbility(StunAbilityHandle);
}

void UStatusComponent::HandleDeathChanged(FGameplayTag Tag, int32 Count)
{
	if (Count > 0) ClearStatuses();
}

void UStatusComponent::ClearStatuses()
{
	if (ASC && ASC->IsOwnerActorAuthoritative())
	{
		FGameplayTagContainer Tags(State_Status);
		Tags.AddTag(State_Poisoned);
		Tags.AddTag(State_Debuff_Slow);
		Tags.AddTag(State_Debuff_WaterBomb);
		Tags.AddTag(State_CrowdControl_Knockback);
		ASC->RemoveActiveEffectsWithGrantedTags(Tags);
	}
}

void UStatusComponent::Uninitialize()
{
	if (!ASC) return;
	ClearStatuses();
	ASC->RegisterGameplayTagEvent(State_Status_Stun).Remove(StunDelegate);
	ASC->RegisterGameplayTagEvent(State_Dead).Remove(DeathDelegate);
	if (ASC->IsOwnerActorAuthoritative())
	{
		ASC->ClearAbility(StunAbilityHandle);
		ASC->RemoveLooseGameplayTag(Capability_Status_Receive, 1, EGameplayTagReplicationState::TagOnly);
	}
	StunAbilityHandle = {};
	ASC = nullptr;
}

void UStatusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Uninitialize();
	Super::EndPlay(Reason);
}
