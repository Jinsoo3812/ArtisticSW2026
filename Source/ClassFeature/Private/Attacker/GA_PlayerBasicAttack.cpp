#include "Attacker/GA_PlayerBasicAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Attacker/BasicMeleeDamageGameplayEffect.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "GASCombatLibrary.h"
#include "Item/Weapons/SwordItem.h"

UGA_PlayerBasicAttack::UGA_PlayerBasicAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_BasicAttack);
	SetAssetTags(AssetTags);
}

void UGA_PlayerBasicAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bAttackFinished = false;
	bHitScanActive = false;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->HasMatchingGameplayTag(State_Dead) || ASC->HasMatchingGameplayTag(State_Attacking))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CacheAttackData())
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_PlayerBasicAttack::ActivateAbility : Missing sword, attack montage, or damage data."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AddAttackStateTag();

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		Player->StopSprint();
	}

	HitScanStartEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_Start,
		nullptr,
		false,
		true);
	if (HitScanStartEventTask)
	{
		HitScanStartEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnHitScanStartEvent);
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
		HitScanEndEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnHitScanEndEvent);
		HitScanEndEventTask->ReadyForActivation();
	}

	if (!PlayAttackMontage())
	{
		FinishAttack(true);
	}
}

void UGA_PlayerBasicAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	EndHitScan();
	RemoveAttackStateTag();

	CachedSword = nullptr;
	CachedAttackMontage = nullptr;
	CachedAttackMontagePlayRate = 1.0f;
	CachedDamageSpecHandle = FGameplayEffectSpecHandle();
	AttackMontageTask = nullptr;
	HitScanStartEventTask = nullptr;
	HitScanEndEventTask = nullptr;
	bAttackFinished = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGA_PlayerBasicAttack::CacheAttackData()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || Player->IsEquipmentTransitioning())
	{
		return false;
	}

	CachedSword = Cast<ASwordItem>(Player->EquippedItem);
	UPlayerEquipmentComponent* EquipmentComponent = Player->GetEquipmentComponent();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!CachedSword || !EquipmentComponent || !SourceASC)
	{
		return false;
	}

	CachedAttackMontage = EquipmentComponent->GetEquippedBasicAttackMontage();
	CachedAttackMontagePlayRate = EquipmentComponent->GetEquippedBasicAttackPlayRate();
	if (!CachedAttackMontage)
	{
		return false;
	}

	TSubclassOf<UGameplayEffect> DamageEffectClass = CachedSword->GetDamageEffectClass();
	if (!DamageEffectClass)
	{
		DamageEffectClass = UBasicMeleeDamageGameplayEffect::StaticClass();
	}

	const float AttackPower = SourceASC->GetNumericAttribute(UBaseAttributeSet::GetAttackPowerAttribute());
	const float Damage = CachedSword->CalculateDamage(AttackPower);
	CachedDamageSpecHandle = UGASCombatLibrary::MakeDamageEffectSpec(
		SourceASC,
		DamageEffectClass,
		Damage,
		Player,
		CachedSword,
		GetAbilityLevel(),
		false);

	return CachedDamageSpecHandle.IsValid();
}

bool UGA_PlayerBasicAttack::PlayAttackMontage()
{
	if (!CachedAttackMontage)
	{
		return false;
	}

	AttackMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("PlayerBasicAttackMontageTask")),
		CachedAttackMontage,
		FMath::Max(CachedAttackMontagePlayRate, KINDA_SMALL_NUMBER),
		NAME_None,
		true);

	if (!AttackMontageTask)
	{
		return false;
	}

	AttackMontageTask->OnCompleted.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageCompleted);
	AttackMontageTask->OnBlendOut.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageBlendOut);
	AttackMontageTask->OnInterrupted.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageInterrupted);
	AttackMontageTask->OnCancelled.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageCancelled);
	AttackMontageTask->ReadyForActivation();
	return true;
}

void UGA_PlayerBasicAttack::OnAttackMontageCompleted()
{
	FinishAttack(false);
}

void UGA_PlayerBasicAttack::OnAttackMontageBlendOut()
{
	FinishAttack(false);
}

void UGA_PlayerBasicAttack::OnAttackMontageInterrupted()
{
	FinishAttack(true);
}

void UGA_PlayerBasicAttack::OnAttackMontageCancelled()
{
	FinishAttack(true);
}

void UGA_PlayerBasicAttack::OnHitScanStartEvent(FGameplayEventData Payload)
{
	StartHitScan();
}

void UGA_PlayerBasicAttack::OnHitScanEndEvent(FGameplayEventData Payload)
{
	EndHitScan();
}

void UGA_PlayerBasicAttack::StartHitScan()
{
	if (bHitScanActive || !CachedSword || !CachedDamageSpecHandle.IsValid())
	{
		return;
	}

	bHitScanActive = true;
	CachedSword->HitScanStart(CachedDamageSpecHandle);
}

void UGA_PlayerBasicAttack::EndHitScan()
{
	if (CachedSword)
	{
		CachedSword->HitScanEnd();
	}

	bHitScanActive = false;
}

void UGA_PlayerBasicAttack::FinishAttack(bool bWasCancelled)
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

void UGA_PlayerBasicAttack::AddAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
}

void UGA_PlayerBasicAttack::RemoveAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}
