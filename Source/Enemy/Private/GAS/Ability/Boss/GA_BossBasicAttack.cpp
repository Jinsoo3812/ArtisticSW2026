#include "GAS/Ability/Boss/GA_BossBasicAttack.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/BossBasicAttackSet.h"
#include "BossAI/ShipBossEnemy.h"
#include "BaseAttributeSet.h"
#include "TimerManager.h"
#include "Weapon/WeaponDataAsset.h"

UGA_BossBasicAttack::UGA_BossBasicAttack()
{
	ActivationOwnedTags.AddTag(State_Boss_Busy);
	ActivationBlockedTags.AddTag(State_Boss_Busy);
	ActivationBlockedTags.AddTag(State_Boss_Hidden);
	ActivationBlockedTags.AddTag(State_Boss_Dashing);
	ActivationBlockedTags.AddTag(State_Dead);
}

bool UGA_BossBasicAttack::PrepareAttack(ABaseEnemy* EnemyOwner)
{
	SelectedAttackId = NAME_None;
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(EnemyOwner);
	const UBossBasicAttackSet* AttackSet = Boss ? Boss->GetBasicAttackSet() : nullptr;
	const FBossBasicAttackEntry* SelectedAttack = AttackSet
		? AttackSet->SelectAttack(
			GetAbilitySystemComponentFromActorInfo(),
			PreviousAttackId,
			FMath::FRand())
		: nullptr;
	if (!SelectedAttack)
	{
		return false;
	}

	SelectedAttackId = SelectedAttack->AttackId;
	return true;
}

bool UGA_BossBasicAttack::ResolveAttackExecutionData(
	ABaseEnemy* EnemyOwner,
	const FWeaponDefinition& WeaponDefinition,
	FEnemyBasicAttackExecutionData& OutData) const
{
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(EnemyOwner);
	const UBossBasicAttackSet* AttackSet = Boss ? Boss->GetBasicAttackSet() : nullptr;
	const FBossBasicAttackEntry* Attack = AttackSet ? AttackSet->FindAttack(SelectedAttackId) : nullptr;
	if (!Attack || !Attack->AttackMontage)
	{
		return false;
	}

	// Reach and damage stay authoritative in the one equipped weapon definition.
	OutData.AttackMontage = Attack->AttackMontage;
	OutData.AttackMontagePlayRate = Attack->AttackMontagePlayRate;
	OutData.DamageEffectClass = WeaponDefinition.CombatData.DamageEffectClass;
	OutData.ImpactGameplayCueTag = WeaponDefinition.CombatData.ImpactGameplayCueTag;
	return OutData.DamageEffectClass != nullptr;
}

void UGA_BossBasicAttack::OnAttackCommitted()
{
	PreviousAttackId = SelectedAttackId;
}

bool UGA_BossBasicAttack::PlayAttackMontage(const FEnemyBasicAttackExecutionData& AttackData)
{
	if (!Super::PlayAttackMontage(AttackData))
	{
		return false;
	}

	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetAvatarActorFromActorInfo());
	const UBossBasicAttackSet* AttackSet = Boss ? Boss->GetBasicAttackSet() : nullptr;
	const FBossBasicAttackEntry* Attack = AttackSet ? AttackSet->FindAttack(SelectedAttackId) : nullptr;
	UWorld* World = GetWorld();
	if (!Attack || !Attack->bUseTimedHitScanWindow || !AttackData.AttackMontage || !World)
	{
		return true;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float AttackSpeedMultiplier = ASC
		? FMath::Clamp(ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()), 0.1f, 3.0f)
		: 1.0f;
	const float EffectivePlayRate = FMath::Max(
		AttackData.AttackMontagePlayRate * AttackSpeedMultiplier,
		KINDA_SMALL_NUMBER);
	const float EffectiveMontageDuration = AttackData.AttackMontage->GetPlayLength() / EffectivePlayRate;
	const float StartDelay = EffectiveMontageDuration * Attack->TimedHitScanStartNormalized;
	const float WindowDuration = EffectiveMontageDuration * Attack->TimedHitScanDurationNormalized;

	World->GetTimerManager().SetTimer(
		TimedHitScanStartHandle,
		this,
		&UGA_BossBasicAttack::OnTimedHitScanStart,
		FMath::Max(StartDelay, KINDA_SMALL_NUMBER),
		false);
	World->GetTimerManager().SetTimer(
		TimedHitScanEndHandle,
		this,
		&UGA_BossBasicAttack::OnTimedHitScanEnd,
		FMath::Max(StartDelay + WindowDuration, KINDA_SMALL_NUMBER),
		false);
	return true;
}

void UGA_BossBasicAttack::OnTimedHitScanStart()
{
	StartHitScan();
}

void UGA_BossBasicAttack::OnTimedHitScanEnd()
{
	EndHitScan();
}

void UGA_BossBasicAttack::ClearTimedHitScanTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimedHitScanStartHandle);
		World->GetTimerManager().ClearTimer(TimedHitScanEndHandle);
	}
}

void UGA_BossBasicAttack::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Every variation first receives the normal shared basic-attack cooldown.
	Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);

	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const AShipBossEnemy* Boss = ActorInfo ? Cast<AShipBossEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UBossBasicAttackSet* AttackSet = Boss ? Boss->GetBasicAttackSet() : nullptr;
	const FBossBasicAttackEntry* Attack = AttackSet ? AttackSet->FindAttack(SelectedAttackId) : nullptr;
	if (!ASC || !Attack || !Attack->IndividualCooldownTag.IsValid()
		|| Attack->IndividualCooldownDuration <= 0.0f)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UEnemyBasicAttackCooldownEffect::StaticClass(),
		GetAbilityLevel(Handle, ActorInfo),
		Context);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return;
	}

	Spec.Data->SetDuration(Attack->IndividualCooldownDuration, true);
	Spec.Data->DynamicGrantedTags.AddTag(Attack->IndividualCooldownTag);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UGA_BossBasicAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	ClearTimedHitScanTimers();
	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
	SelectedAttackId = NAME_None;
}
