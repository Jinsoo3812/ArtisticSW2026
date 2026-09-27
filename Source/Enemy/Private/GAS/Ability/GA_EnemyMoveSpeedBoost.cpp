#include "GAS/Ability/GA_EnemyMoveSpeedBoost.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "GAS/EnemyAttributeSet.h"

UEnemyMoveSpeedBoostEffect::UEnemyMoveSpeedBoostEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
	// UE 5.7 exposes SetStackingType without a GameplayAbilities DLL export.
	// Keep constructor authoring compatible until the setter is exported.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateBySource;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;

	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UEnemyAttributeSet::GetMoveSpeedBonusAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat SetByCallerBonus;
	SetByCallerBonus.DataTag = Data_Effect_MoveSpeedBonus;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerBonus);
}

UEnemyMoveSpeedBoostCooldownEffect::UEnemyMoveSpeedBoostCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
}

UGA_EnemyMoveSpeedBoost::UGA_EnemyMoveSpeedBoost()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer NativeAbilityTags;
	NativeAbilityTags.AddTag(GameplayAbility_Enemy_Buff_MoveSpeed);
	SetAssetTags(NativeAbilityTags);

	ActivationBlockedTags.AddTag(State_Dead);
	NativeCooldownTags.AddTag(Cooldown_Enemy_Buff_MoveSpeed);
	MoveSpeedBoostEffectClass = UEnemyMoveSpeedBoostEffect::StaticClass();
}

const FGameplayTagContainer* UGA_EnemyMoveSpeedBoost::GetCooldownTags() const
{
	return &NativeCooldownTags;
}

void UGA_EnemyMoveSpeedBoost::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || CooldownDuration <= 0.0f || NativeCooldownTags.IsEmpty())
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UEnemyMoveSpeedBoostCooldownEffect::StaticClass(),
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

void UGA_EnemyMoveSpeedBoost::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!ASC || !Avatar || !Avatar->HasAuthority() || !MoveSpeedBoostEffectClass
		|| MoveSpeedBonus <= 0.0f || BuffDuration <= 0.0f
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Avatar, Avatar);
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		MoveSpeedBoostEffectClass,
		GetAbilityLevel(Handle, ActorInfo),
		Context);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Spec.Data->SetDuration(BuffDuration, true);
	Spec.Data->SetSetByCallerMagnitude(Data_Effect_MoveSpeedBonus, MoveSpeedBonus);
	Spec.Data->DynamicGrantedTags.AddTag(State_Buff_MoveSpeed);
	const FActiveGameplayEffectHandle AppliedHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !AppliedHandle.IsValid());
}
