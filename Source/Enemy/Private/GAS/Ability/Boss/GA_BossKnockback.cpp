#include "GAS/Ability/Boss/GA_BossKnockback.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "GameFramework/Character.h"
#include "GASDamageInstantGameplayEffect.h"
#include "ShipAI/EnemyShip.h"

UGA_BossKnockback::UGA_BossKnockback()
{
	SetBossAbilityTags(GameplayAbility_Boss_Knockback, Cooldown_Boss_Knockback);
	CooldownDuration = 6.0f;
	DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	ImpactGameplayCueTag = GameplayCue_Impact_Boss_Knockback;
}

void UGA_BossKnockback::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	AShipBossEnemy* Boss = GetBossAvatar();
	CachedTarget = GetBossTarget();
	if (!Boss || !CachedTarget
		|| FVector::Dist2D(Boss->GetActorLocation(), CachedTarget->GetActorLocation()) > AttackRange
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishKnockback(true);
		return;
	}

	if (AttackMontage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("BossKnockbackMontage"), AttackMontage);
		if (MontageTask)
		{
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_BossKnockback::HandleMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_BossKnockback::HandleMontageInterrupted);
			MontageTask->ReadyForActivation();
		}
	}

	if (ImpactDelay <= 0.0f)
	{
		ApplyImpact();
		return;
	}
	ImpactDelayTask = UAbilityTask_WaitDelay::WaitDelay(this, ImpactDelay);
	ImpactDelayTask->OnFinish.AddDynamic(this, &UGA_BossKnockback::ApplyImpact);
	ImpactDelayTask->ReadyForActivation();
}

void UGA_BossKnockback::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	CachedTarget = nullptr;
	MontageTask = nullptr;
	ImpactDelayTask = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BossKnockback::ApplyImpact()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = CachedTarget.Get();
	if (!Boss || !Target || Boss->GetBossCombatTarget() != Target
		|| FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation()) > AttackRange + 50.0f)
	{
		FinishKnockback(false);
		return;
	}

	ApplyDamageToTarget(Target, DamageEffectClass, Damage);
	if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
	{
		ApplyTimedStateTag(*TargetASC, State_CrowdControl_Knockback, KnockbackStateDuration);
	}

	if (ACharacter* TargetCharacter = Cast<ACharacter>(Target))
	{
		const FVector DeckUp = Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh()
			? Boss->GetHostShip()->GetShipDeckMesh()->GetUpVector().GetSafeNormal()
			: FVector::UpVector;
		FVector Backward = FVector::VectorPlaneProject(-Target->GetActorForwardVector(), DeckUp).GetSafeNormal();
		if (Backward.IsNearlyZero())
		{
			Backward = FVector::VectorPlaneProject(Target->GetActorLocation() - Boss->GetActorLocation(), DeckUp).GetSafeNormal();
		}
		TargetCharacter->LaunchCharacter(
			Backward * HorizontalLaunchSpeed + DeckUp * VerticalLaunchSpeed,
			true,
			true);
	}

	FinishKnockback(false);
}

void UGA_BossKnockback::HandleMontageInterrupted()
{
	FinishKnockback(true);
}

void UGA_BossKnockback::FinishKnockback(bool bWasCancelled)
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}
