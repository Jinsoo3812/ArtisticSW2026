#include "GAS/Ability/Boss/GA_BossChainVanish.h"

#include "AbilitySystemComponent.h"
#include "AI/PointSelectionFailure.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossChainVanish, Log, All);

UGA_BossChainVanish::UGA_BossChainVanish()
{
	SetVanishTags(GameplayAbility_Boss_ChainVanish, Cooldown_Boss_ChainVanish);
	CooldownDuration = 10.f;
}

bool UGA_BossChainVanish::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	const auto* Instance = Spec ? Cast<UGA_BossChainVanish>(Spec->GetPrimaryInstance()) : nullptr;
	// BT decorators query the CDO. Consult the per-boss instance, including during the
	// Combat-tag callback before the initial cooldown effect has been applied.
	if (!ASC || !ASC->HasMatchingGameplayTag(AI_State_Boss_Combat) || !Instance || !Instance->bInitialCooldownApplied)
		return false;
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UGA_BossChainVanish::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	if (!IsInstantiated() || !ActorInfo || !ActorInfo->AvatarActor.IsValid() || !ActorInfo->AvatarActor->HasAuthority()) return;
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC || CombatASC.Get() == ASC) return;
	UnbindCombatState();
	CombatASC = ASC;
	GrantedHandle = Spec.Handle;
	bInitialCooldownApplied = false;
	CombatStateDelegate = ASC->RegisterGameplayTagEvent(AI_State_Boss_Combat, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleCombatStateChanged);
	// Covers deferred grants after the boss has already entered combat.
	if (ASC->HasMatchingGameplayTag(AI_State_Boss_Combat)) HandleCombatStateChanged(AI_State_Boss_Combat, 1);
}

void UGA_BossChainVanish::HandleCombatStateChanged(FGameplayTag Tag, int32 NewCount)
{
	if (NewCount <= 0 || bInitialCooldownApplied) return;
	UAbilitySystemComponent* ASC = CombatASC.Get();
	if (!ASC || !ASC->AbilityActorInfo.IsValid()) return;
	ApplyVanishCooldown(GrantedHandle, ASC->AbilityActorInfo.Get());
	bInitialCooldownApplied = true;
	UE_LOG(LogBossChainVanish, Log, TEXT("ChainVanish initial combat cooldown: %.2fs Boss=%s"), CooldownDuration, *GetNameSafe(GetBossAvatar()));
}

void UGA_BossChainVanish::UnbindCombatState()
{
	if (UAbilitySystemComponent* ASC = CombatASC.Get(); ASC && CombatStateDelegate.IsValid())
		ASC->RegisterGameplayTagEvent(AI_State_Boss_Combat, EGameplayTagEventType::NewOrRemoved).Remove(CombatStateDelegate);
	CombatStateDelegate.Reset();
	CombatASC.Reset();
}

void UGA_BossChainVanish::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	UnbindCombatState();
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UGA_BossChainVanish::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	CompletedVanishCount = 0;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_BossChainVanish::HandleRevealedAtDestination()
{
	++CompletedVanishCount;
	if (CanAttackFromCurrentPosition())
	{
		UE_LOG(LogBossChainVanish, Log, TEXT("ChainVanish attacks after %d relocation(s). Boss=%s"), CompletedVanishCount, *GetNameSafe(GetBossAvatar()));
		StartAttackAtDestination();
		return;
	}
	if (CompletedVanishCount >= FMath::Clamp(MaximumVanishCount, 1, 3))
	{
		UE_LOG(LogBossChainVanish, Log, TEXT("ChainVanish exhausted without an attack. Boss=%s"), *GetNameSafe(GetBossAvatar()));
		FinishAttack(true);
		return;
	}
	AShipBossEnemy* Boss = GetBossAvatar();
	FDeckWalkLocation Destination;
	if (!Boss || !IsVanishTargetValid()) { FinishAttack(true); return; }
	if (!UBossDeckPointSelector::SelectDestinationLocation(Boss->GetHostShip(), Boss, GetVanishTarget(),
			EBossDestinationPurpose::Vanish, EBossDestinationRelation::BehindTarget, RetrySelectionSettings, Destination)
		|| !Boss->TrySetDestinationLocation(Destination, false))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("No valid ChainVanish relocation point."));
		FinishAttack(true);
		return;
	}
	if (!StartPreparedRelocation()) FinishAttack(true);
}
