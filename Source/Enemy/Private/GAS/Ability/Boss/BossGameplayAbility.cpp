#include "GAS/Ability/Boss/BossGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "GASCombatLibrary.h"
#include "Components/CombatHitResolverComponent.h"
#include "GAS/SWCombatEffectContextLibrary.h"

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
	ActivationBlockedTags.AddTag(State_Attacking);
	ActivationBlockedTags.AddTag(State_Damaged);
	ActivationBlockedTags.AddTag(State_Dead);
}

void UBossGameplayAbility::ExecuteStartupGameplayCue() const
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!Boss || !Boss->HasAuthority() || !ASC || !StartupGameplayCueTag.IsValid())
	{
		return;
	}

	FGameplayCueParameters Parameters;
	Parameters.Location = Boss->GetActorLocation();
	Parameters.Normal = Boss->GetActorForwardVector();
	Parameters.Instigator = Boss;
	Parameters.EffectCauser = Boss;
	Parameters.bReplicateLocationWhenUsingMinimalRepProxy = true;
	ASC->ExecuteGameplayCue(StartupGameplayCueTag, Parameters);
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

bool UBossGameplayAbility::PrepareStrengthAttack(float AttackCoefficient)
{
	FStrengthDamageRequest Request;
	Request.SourceASC = GetAbilitySystemComponentFromActorInfo();
	Request.InstigatorActor = GetAvatarActorFromActorInfo();
	Request.EffectCauser = GetAvatarActorFromActorInfo();

	Request.AttackCoefficient = AttackCoefficient;
	CommittedDamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);
	if (!CommittedDamageSpec.IsValid()) return false;
	if (ImpactGameplayCueTag.IsValid()) CommittedDamageSpec.Data->AddDynamicAssetTag(ImpactGameplayCueTag);
	auto* Resolver = UCombatHitResolverComponent::GetOrCreate(Request.EffectCauser);
	return Resolver && Resolver->OpenWindow(CommittedDamageSpec);
}

bool UBossGameplayAbility::ApplyDamageToTarget(AActor* Target, const FHitResult* HitResult) const
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss || !Boss->HasAuthority() || !IsActive() || !CommittedDamageSpec.IsValid()) return false;
	auto* Resolver = Boss->FindComponentByClass<UCombatHitResolverComponent>();
	return Resolver && Resolver->ResolveHit(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target),
		HitResult ? *HitResult : FHitResult());
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

	FGameplayEffectContextHandle Context =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC, Boss, Boss, TargetASC.GetAvatarActor());
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

void UBossGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (AActor* Avatar = GetAvatarActorFromActorInfo())
		if (auto* Resolver = Avatar->FindComponentByClass<UCombatHitResolverComponent>()) Resolver->CloseWindow();
	CommittedDamageSpec = FGameplayEffectSpecHandle();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
