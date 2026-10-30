#include "GAS/Ability/Boss/GA_BossGunAttack.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/GunnerBossTags.h"
#include "BossAI/BossGunCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "BaseGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/CombatHitResolverComponent.h"
#include "TimerManager.h"

UGA_BossGunAttack::UGA_BossGunAttack()
{
	SetBossAbilityTags(GunnerBossTags::Shot, GunnerBossTags::ShotCooldown);
	// Ordinary hit reactions cannot restart/cancel aiming or steal its full-body montage.
	SetAssetTags(FGameplayTagContainer(GunnerBossTags::Shot));
	BlockAbilitiesWithTag.AddTag(GameplayAbility_HitReaction);
	ActivationBlockedTags.AddTag(State_Status_Stun);
	ActivationBlockedTags.AddTag(State_Status_Knockback);
}

UGA_BossGunBurst::UGA_BossGunBurst()
{
	SetBossAbilityTags(GunnerBossTags::Burst, GunnerBossTags::BurstCooldown);
	SetAssetTags(FGameplayTagContainer(GunnerBossTags::Burst));
}

bool UGA_BossGunAttack::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const AGunnerBossEnemy* Boss = ActorInfo ? Cast<AGunnerBossEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Boss && Boss->GunCombat && Boss->GunCombat->CanStart(Boss->GetBossCombatTarget())
		&& Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UGA_BossGunAttack::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const AGunnerBossEnemy* Boss = ActorInfo ? Cast<AGunnerBossEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (Boss && Boss->CombatData && ASC)
	{
		const float Duration = IsBurst() ? Boss->CombatData->Burst.Cooldown : Boss->CombatData->Basic.Cooldown;
		ApplyTaggedCooldown(*ASC, this, *GetCooldownTags(), Duration, GetAbilityLevel(Handle, ActorInfo));
	}
}

void UGA_BossGunAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	AGunnerBossEnemy* Boss = Cast<AGunnerBossEnemy>(GetBossAvatar());
	AttackId = 0;
	bCommitted = false;
	ShotsFired = 0;
	if (!Boss || !Boss->CombatData || !Boss->GunCombat) { Finish(true); return; }
	Combat = Boss->GunCombat;
	AttackTarget = Boss->GetBossCombatTarget();
	Pattern = IsBurst() ? Boss->CombatData->Burst : Boss->CombatData->Basic;
	ShotCount = IsBurst() ? Boss->CombatData->BurstShotCount : 1;
	ImpactGameplayCueTag = Boss->CombatData->ImpactCue;
	AttackId = Combat->BeginAttack(AttackTarget);
	if (AttackId == 0 || !CommitAbility(Handle, ActorInfo, ActivationInfo)) { Finish(true); return; }
	bCommitted = true;
	if (Pattern.Montage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, TEXT("GunnerAttack"),
			Pattern.Montage, Pattern.MontagePlayRate, Pattern.PrepareSection);
		if (MontageTask) MontageTask->ReadyForActivation();
		if (!IsActive()) return;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			if (!Pattern.PrepareSection.IsNone()) ASC->CurrentMontageSetNextSectionName(Pattern.PrepareSection, Pattern.PrepareSection);
			if (!Pattern.RecoverySection.IsNone()) ASC->CurrentMontageSetNextSectionName(Pattern.RecoverySection, Pattern.RecoverySection);
		}
	}
	GetWorld()->GetTimerManager().SetTimer(AimTimer, this, &UGA_BossGunAttack::UpdateAim,
		Boss->CombatData->AimUpdateInterval, true);
	BeginPreparation();
}

void UGA_BossGunAttack::Schedule(void (UGA_BossGunAttack::*Callback)(), double Deadline)
{
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateUObject(this, Callback),
		FMath::Max(static_cast<float>(Deadline - GetWorld()->GetTimeSeconds()), KINDA_SMALL_NUMBER), false);
}

void UGA_BossGunAttack::JumpMontage(FName Section)
{
	if (Pattern.Montage && !Section.IsNone())
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo()) ASC->CurrentMontageJumpToSection(Section);
}

void UGA_BossGunAttack::BeginPreparation()
{
	if (!IsActive() || !Combat || !Combat->OwnsAttack(AttackId) || !Combat->IsRuntimeValid(AttackTarget)) { Finish(true); return; }
	bFiredCurrentStep = false;
	Phase = EBossGunAimPhase::Tracking;
	PhaseStart = GetWorld()->GetTimeSeconds();
	PreparationEnd = PhaseStart + Pattern.PrepareTime;
	PhaseEnd = PreparationEnd - Pattern.LockTime;
	Combat->SetHiddenPhase(AttackId, EBossGunAimPhase::Hidden, PhaseStart, PhaseEnd);
	JumpMontage(Pattern.PrepareSection);
	UpdateAim();
	if (!IsActive()) return;
	if (PhaseEnd <= PhaseStart) BeginLock();
	else Schedule(&UGA_BossGunAttack::BeginLock, PhaseEnd);
}

void UGA_BossGunAttack::BeginLock()
{
	if (!IsActive() || !Combat || !Combat->OwnsAttack(AttackId)) { Finish(true); return; }
	// Refresh tracking at the exact lock boundary, then freeze this direction in deck space.
	UpdateAim();
	if (!IsActive()) return;
	if (!Combat->LockAim(AttackId, AttackTarget)) { Finish(true); return; }
	Phase = EBossGunAimPhase::Locked;
	PhaseStart = GetWorld()->GetTimeSeconds();
	PhaseEnd = PreparationEnd;
	UpdateAim();
	if (IsActive()) Schedule(&UGA_BossGunAttack::FireShot, PreparationEnd);
}

void UGA_BossGunAttack::UpdateAim()
{
	if (!IsActive()) return;
	if (!Combat || !Combat->OwnsAttack(AttackId) || !Combat->IsRuntimeValid(AttackTarget)) { Finish(true); return; }
	if (Phase != EBossGunAimPhase::Tracking && Phase != EBossGunAimPhase::Locked) return;
	if (!Combat->UpdateAim(AttackId, AttackTarget, Phase, PhaseStart, PhaseEnd)) Finish(true);
}

void UGA_BossGunAttack::FireShot()
{
	if (!IsActive() || bFiredCurrentStep || Phase != EBossGunAimPhase::Locked) return;
	if (!Combat || !Combat->OwnsAttack(AttackId) || !Combat->IsRuntimeValid(AttackTarget)) { Finish(true); return; }
	bFiredCurrentStep = true;
	FHitResult Hit;
	FVector Start, End;
	// New damage spec/window per shot permits the next burst shot to hit the same player.
	if (!Combat->BuildShot(AttackId, Hit, Start, End) || !PrepareStrengthAttack(Pattern.DamageCoefficient)) { Finish(true); return; }
	if (Hit.bBlockingHit && Combat->IsLiveTarget(Hit.GetActor()))
	{
		// The authoritative gun trace already checked all cover on the configured channel.
		// Do not apply a second, differently filtered world-static occlusion policy.
		if (auto* Resolver = GetAvatarActorFromActorInfo()->FindComponentByClass<UCombatHitResolverComponent>())
			Resolver->ResolveHit(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor()), Hit, true, false, false);
	}
	if (!IsActive() || !Combat->OwnsAttack(AttackId)) return;
	Combat->ShowShot(AttackId, Start, End);
	++ShotsFired;
	if (Pattern.Montage && !Pattern.FireSection.IsNone())
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
			ASC->CurrentMontageSetNextSectionName(Pattern.FireSection,
				ShotsFired >= ShotCount ? Pattern.RecoverySection : Pattern.PrepareSection);
	JumpMontage(Pattern.FireSection);
	Phase = EBossGunAimPhase::Hidden;
	const float Now = GetWorld()->GetTimeSeconds();
	Combat->SetHiddenPhase(AttackId, Phase, Now, Now + Pattern.ShotGap);
	if (ShotsFired >= ShotCount) BeginRecovery();
	else if (Pattern.ShotGap <= 0.f) BeginPreparation();
	else Schedule(&UGA_BossGunAttack::BeginPreparation, Now + Pattern.ShotGap);
}

void UGA_BossGunAttack::BeginRecovery()
{
	Phase = EBossGunAimPhase::Recovery;
	PhaseStart = GetWorld()->GetTimeSeconds();
	PhaseEnd = PhaseStart + Pattern.RecoveryTime;
	Combat->SetHiddenPhase(AttackId, Phase, PhaseStart, PhaseEnd);
	// Let the firing animation finish naturally into recovery rather than skipping it.
	if (Pattern.FireSection.IsNone()) JumpMontage(Pattern.RecoverySection);
	if (Pattern.RecoveryTime <= 0.f) CompleteAttack();
	else Schedule(&UGA_BossGunAttack::CompleteAttack, PhaseEnd);
}

void UGA_BossGunAttack::CompleteAttack() { Finish(false); }
void UGA_BossGunAttack::Finish(bool bCancelled)
{
	if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
}

void UGA_BossGunAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PhaseTimer);
		GetWorld()->GetTimerManager().ClearTimer(AimTimer);
	}
	if (Combat) Combat->FinishAttack(AttackId, bCommitted ? Pattern.PostAttackGap : 0.f, !bWasCancelled);
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && ASC->GetAnimatingAbility() == this)
		ASC->CurrentMontageStop();
	AttackId = 0;
	bCommitted = false;
	Phase = EBossGunAimPhase::Hidden;
	AttackTarget = nullptr;
	Combat = nullptr;
	MontageTask = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
