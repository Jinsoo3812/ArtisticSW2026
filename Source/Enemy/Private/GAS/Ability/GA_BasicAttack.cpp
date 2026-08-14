// Fill out your copyright notice in the Description page of Project Settings.


#include "GAS/Ability/GA_BasicAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BaseAttributeSet.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/WeaponDataAsset.h"

UEnemyBasicAttackCooldownEffect::UEnemyBasicAttackCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
}

UGA_BasicAttack::UGA_BasicAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer BasicAttackTags;
	BasicAttackTags.AddTag(GameplayAbility_BasicAttack);
	BasicAttackTags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(BasicAttackTags);
	ActivationBlockedTags.AddTag(State_Damaged);
	NativeCooldownTags.AddTag(Cooldown_Enemy_BasicAttack);
}

const FGameplayTagContainer* UGA_BasicAttack::GetCooldownTags() const
{
	return &NativeCooldownTags;
}

void UGA_BasicAttack::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || NativeCooldownTags.IsEmpty() || AttackCooldownDuration <= 0.0f)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(
		UEnemyBasicAttackCooldownEffect::StaticClass(),
		GetAbilityLevel(Handle, ActorInfo),
		Context);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return;
	}

	SpecHandle.Data->SetDuration(AttackCooldownDuration, true);
	SpecHandle.Data->DynamicGrantedTags.AppendTags(NativeCooldownTags);
	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

void UGA_BasicAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());
	if (!EnemyOwner)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FWeaponDefinition* WeaponDefinition = CacheAttackData(EnemyOwner);
	if (!WeaponDefinition)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AddAttackStateTag();

	HitScanStartEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_Start,
		nullptr,
		false,
		true);
	if (HitScanStartEventTask)
	{
		HitScanStartEventTask->EventReceived.AddDynamic(this, &UGA_BasicAttack::OnHitScanStartEvent);
		HitScanStartEventTask->ReadyForActivation();
	}

	HitScanEndEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_End,
		nullptr,
		false,
		true);
	if (HitScanEndEventTask)
	{
		HitScanEndEventTask->EventReceived.AddDynamic(this, &UGA_BasicAttack::OnHitScanEndEvent);
		HitScanEndEventTask->ReadyForActivation();
	}

	if (PlayAttackMontage(*WeaponDefinition))
	{
		return;
	}

	FinishAttack(false);
}

void UGA_BasicAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	EndHitScan();
	RemoveAttackStateTag();

	CachedWeapon = nullptr;
	CachedDamageSpecHandle = FGameplayEffectSpecHandle();
	AttackMontageTask = nullptr;
	HitScanStartEventTask = nullptr;
	HitScanEndEventTask = nullptr;
	bAttackFinished = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

const FWeaponDefinition* UGA_BasicAttack::CacheAttackData(ABaseEnemy* EnemyOwner)
{
	if (!EnemyOwner)
	{
		return nullptr;
	}

	UBaseWeaponComponent* WeaponComponent = EnemyOwner->GetWeaponComponent();
	CachedWeapon = WeaponComponent ? WeaponComponent->GetCurrentWeapon() : nullptr;
	const FWeaponDefinition* WeaponDefinition = WeaponComponent ? WeaponComponent->GetCurrentWeaponDefinition() : nullptr;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!CachedWeapon || !WeaponDefinition || !SourceASC || !WeaponDefinition->CombatData.DamageEffectClass)
	{
		return nullptr;
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	ContextHandle.AddInstigator(EnemyOwner, CachedWeapon);
	ContextHandle.AddSourceObject(CachedWeapon);

	CachedDamageSpecHandle = SourceASC->MakeOutgoingSpec(
		WeaponDefinition->CombatData.DamageEffectClass,
		1,
		ContextHandle);

	return CachedDamageSpecHandle.IsValid() ? WeaponDefinition : nullptr;
}

bool UGA_BasicAttack::PlayAttackMontage(const FWeaponDefinition& WeaponDefinition)
{
	UAnimMontage* AttackMontage = WeaponDefinition.CombatData.AttackMontage;
	if (!AttackMontage)
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float AttackSpeedMultiplier = ASC
		? FMath::Clamp(ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()), 0.1f, 3.0f)
		: 1.0f;
	const float EffectivePlayRate = WeaponDefinition.CombatData.AttackMontagePlayRate * AttackSpeedMultiplier;

	AttackMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("EnemyBasicAttackMontageTask")),
		AttackMontage,
		FMath::Max(EffectivePlayRate, KINDA_SMALL_NUMBER),
		NAME_None,
		true);

	if (!AttackMontageTask)
	{
		return false;
	}

	AttackMontageTask->OnCompleted.AddDynamic(this, &UGA_BasicAttack::OnAttackMontageCompleted);
	AttackMontageTask->OnBlendOut.AddDynamic(this, &UGA_BasicAttack::OnAttackMontageBlendOut);
	AttackMontageTask->OnInterrupted.AddDynamic(this, &UGA_BasicAttack::OnAttackMontageInterrupted);
	AttackMontageTask->OnCancelled.AddDynamic(this, &UGA_BasicAttack::OnAttackMontageCancelled);
	AttackMontageTask->ReadyForActivation();

	return true;
}

void UGA_BasicAttack::OnAttackMontageCompleted()
{
	FinishAttack(false);
}

void UGA_BasicAttack::OnAttackMontageBlendOut()
{
	FinishAttack(false);
}

void UGA_BasicAttack::OnAttackMontageInterrupted()
{
	FinishAttack(true);
}

void UGA_BasicAttack::OnAttackMontageCancelled()
{
	FinishAttack(true);
}

void UGA_BasicAttack::OnHitScanStartEvent(FGameplayEventData Payload)
{
	StartHitScan();
}

void UGA_BasicAttack::OnHitScanEndEvent(FGameplayEventData Payload)
{
	EndHitScan();
}

void UGA_BasicAttack::StartHitScan()
{
	if (bHitScanActive || !CachedWeapon || !CachedDamageSpecHandle.IsValid())
	{
		return;
	}

	bHitScanActive = true;
	CachedWeapon->HitScanStart(CachedDamageSpecHandle);
}

void UGA_BasicAttack::EndHitScan()
{
	if (!bHitScanActive)
	{
		return;
	}

	bHitScanActive = false;

	if (CachedWeapon)
	{
		CachedWeapon->HitScanEnd();
	}
}

void UGA_BasicAttack::FinishAttack(bool bWasCancelled)
{
	if (bAttackFinished)
	{
		return;
	}

	bAttackFinished = true;

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_BasicAttack::AddAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
}

void UGA_BasicAttack::RemoveAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}
