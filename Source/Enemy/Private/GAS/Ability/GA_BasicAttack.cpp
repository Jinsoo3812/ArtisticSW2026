// Fill out your copyright notice in the Description page of Project Settings.


#include "GAS/Ability/GA_BasicAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BaseAttributeSet.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/WeaponDataAsset.h"
#include "GAS/SWCombatEffectContextLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyBasicAttack, Log, All);

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
	ActivationBlockedTags.AddTag(State_Boss_Busy);
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

	ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());
	if (!EnemyOwner)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FEnemyBasicAttackExecutionData AttackData;
	if (!PrepareAttack(EnemyOwner) || !CacheAttackData(EnemyOwner, AttackData))
	{
		UE_LOG(LogEnemyBasicAttack, Warning,
			TEXT("Basic attack has no valid weapon damage data. Enemy=%s SourceObject=%s"),
			*GetNameSafe(EnemyOwner), *GetNameSafe(GetCurrentSourceObject()));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	OnAttackCommitted();

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

	if (PlayAttackMontage(AttackData))
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

bool UGA_BasicAttack::PrepareAttack(ABaseEnemy* EnemyOwner)
{
	return EnemyOwner != nullptr;
}

bool UGA_BasicAttack::ResolveAttackExecutionData(
	ABaseEnemy* EnemyOwner,
	const FWeaponDefinition& WeaponDefinition,
	FEnemyBasicAttackExecutionData& OutData) const
{
	OutData.AttackMontage = WeaponDefinition.CombatData.AttackMontage;
	OutData.AttackMontagePlayRate = WeaponDefinition.CombatData.AttackMontagePlayRate;
	OutData.DamageEffectClass = WeaponDefinition.CombatData.DamageEffectClass;
	OutData.ImpactGameplayCueTag = WeaponDefinition.CombatData.ImpactGameplayCueTag;
	return OutData.AttackMontage && OutData.DamageEffectClass;
}

void UGA_BasicAttack::OnAttackCommitted()
{
}

bool UGA_BasicAttack::CacheAttackData(
	ABaseEnemy* EnemyOwner,
	FEnemyBasicAttackExecutionData& OutData)
{
	if (!EnemyOwner)
	{
		return false;
	}

	UBaseWeaponComponent* WeaponComponent = EnemyOwner->GetWeaponComponent();
	CachedWeapon = WeaponComponent ? WeaponComponent->GetCurrentWeapon() : nullptr;
	const FWeaponDefinition* WeaponDefinition = WeaponComponent ? WeaponComponent->GetCurrentWeaponDefinition() : nullptr;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!CachedWeapon || !WeaponDefinition || !SourceASC
		|| !ResolveAttackExecutionData(EnemyOwner, *WeaponDefinition, OutData)
		|| !OutData.DamageEffectClass)
	{
		return false;
	}

	FGameplayEffectContextHandle ContextHandle =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC, EnemyOwner, CachedWeapon);

	CachedDamageSpecHandle = SourceASC->MakeOutgoingSpec(
		OutData.DamageEffectClass,
		1,
		ContextHandle);
	if (CachedDamageSpecHandle.IsValid() && CachedDamageSpecHandle.Data.IsValid()
		&& OutData.ImpactGameplayCueTag.IsValid())
	{
		CachedDamageSpecHandle.Data->AddDynamicAssetTag(
			OutData.ImpactGameplayCueTag);
	}

	return CachedDamageSpecHandle.IsValid();
}

bool UGA_BasicAttack::PlayAttackMontage(const FEnemyBasicAttackExecutionData& AttackData)
{
	UAnimMontage* AttackMontage = AttackData.AttackMontage;
	if (!AttackMontage)
	{
		UE_LOG(LogEnemyBasicAttack, Warning,
			TEXT("Basic attack weapon has no AttackMontage. Weapon=%s"),
			*GetNameSafe(CachedWeapon));
		return false;
	}

	const ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());
	if (!EnemyOwner || !EnemyOwner->GetMesh() || !EnemyOwner->GetMesh()->GetAnimInstance())
	{
		UE_LOG(LogEnemyBasicAttack, Warning,
			TEXT("Basic attack cannot play montage because the enemy AnimInstance is missing. Enemy=%s Montage=%s"),
			*GetNameSafe(EnemyOwner), *GetNameSafe(AttackMontage));
		return false;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float AttackSpeedMultiplier = ASC
		? FMath::Clamp(ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()), 0.1f, 3.0f)
		: 1.0f;
	const float EffectivePlayRate = AttackData.AttackMontagePlayRate * AttackSpeedMultiplier;
	UE_LOG(LogEnemyBasicAttack, Verbose,
		TEXT("Playing basic attack montage. Enemy=%s Ability=%s Source=%s Montage=%s Rate=%.2f"),
		*GetNameSafe(EnemyOwner), *GetNameSafe(this), *GetNameSafe(GetCurrentSourceObject()),
		*GetNameSafe(AttackMontage), EffectivePlayRate);

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
		if (Cast<AShipBossEnemy>(GetAvatarActorFromActorInfo()))
		{
			ASC->AddLooseGameplayTag(State_Boss_Busy);
		}
	}
}


void UGA_BasicAttack::RemoveAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
		if (Cast<AShipBossEnemy>(GetAvatarActorFromActorInfo()))
		{
			ASC->RemoveLooseGameplayTag(State_Boss_Busy);
		}
	}
}
