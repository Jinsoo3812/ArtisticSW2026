#include "GAS/Ability/GA_RangedEnemyAttack.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "RangedEnemy/RangedEnemy.h"
#include "Weapon/EnemyBow.h"

UGA_RangedEnemyAttack::UGA_RangedEnemyAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer RangedAbilityTags;
	RangedAbilityTags.AddTag(GameplayAbility_BasicAttack);
	RangedAbilityTags.AddTag(GameplayAbility_RangedAttack);
	RangedAbilityTags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(RangedAbilityTags);
	ActivationBlockedTags.AddTag(State_Damaged);
}

void UGA_RangedEnemyAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	CachedEnemy = Cast<ARangedEnemy>(GetAvatarActorFromActorInfo());
	CachedTarget = CachedEnemy ? CachedEnemy->GetCombatTarget() : nullptr;
	bProjectileFired = false;
	bFinishingAttack = false;
	bOwnsServerPoseRefresh = false;

	if (!CachedEnemy)
	{
		FinishAttack(true);
		return;
	}

	if (!CachedEnemy->HasAuthority())
	{
		FinishAttack(true);
		return;
	}
	if (!CachedEnemy->CanAttackCurrentTarget(true))
	{
		FinishAttack(true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishAttack(true);
		return;
	}

	CachedEnemy->AcquireServerRangedAttackPoseRefresh();
	bOwnsServerPoseRefresh = true;
	AddAttackStateTag();

	const FGameplayTag FireEventTag = CachedEnemy->GetRangedFireEventTag();
	if (CachedEnemy->GetRangedAttackMontage() && FireEventTag.IsValid())
	{
		FireProjectileEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			FireEventTag,
			nullptr,
			false,
			true);
		if (FireProjectileEventTask)
		{
			FireProjectileEventTask->EventReceived.AddDynamic(this, &UGA_RangedEnemyAttack::OnFireProjectileEvent);
			FireProjectileEventTask->ReadyForActivation();
		}
	}

	if (PlayAttackMontage())
	{
		return;
	}

	FireProjectile();
	FinishAttack(false);
}

void UGA_RangedEnemyAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	RemoveAttackStateTag();
	if (bOwnsServerPoseRefresh && CachedEnemy)
	{
		CachedEnemy->ReleaseServerRangedAttackPoseRefresh();
	}
	CachedEnemy = nullptr;
	CachedTarget = nullptr;
	AttackMontageTask = nullptr;
	FireProjectileEventTask = nullptr;
	bProjectileFired = false;
	bFinishingAttack = false;
	bOwnsServerPoseRefresh = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_RangedEnemyAttack::OnFireProjectileEvent(FGameplayEventData Payload)
{
	FireProjectile();
}

void UGA_RangedEnemyAttack::OnAttackMontageCompleted()
{
	if (!bProjectileFired)
	{
		// A missing montage notify must not turn the enemy into a silent soft-lock.
		// The editor guide still asks for a FireArrow event at the release frame.
		FireProjectile();
	}
	FinishAttack(false);
}

void UGA_RangedEnemyAttack::OnAttackMontageBlendOut()
{
	if (!bFinishingAttack)
	{
		OnAttackMontageCompleted();
	}
}

void UGA_RangedEnemyAttack::OnAttackMontageInterrupted()
{
	FinishAttack(true);
}

void UGA_RangedEnemyAttack::OnAttackMontageCancelled()
{
	FinishAttack(true);
}

bool UGA_RangedEnemyAttack::FireProjectile()
{
	if (!CachedEnemy)
	{
		return false;
	}
	if (bProjectileFired)
	{
		return false;
	}
	if (!CachedTarget)
	{
		return false;
	}
	if (CachedEnemy->GetCombatTarget() != CachedTarget)
	{
		return false;
	}
	if (!CachedEnemy->CanAttackTarget(CachedTarget, true))
	{
		return false;
	}

	AEnemyBow* Bow = CachedEnemy->GetEquippedBow();
	FTransform ArrowSpawnTransform;
	if (!Bow || !CachedEnemy->GetRangedAttackOrigin(ArrowSpawnTransform))
	{
		return false;
	}

	UWorld* World = CachedEnemy->GetWorld();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	TSubclassOf<AArrowProjectile> ProjectileClass = Bow->GetProjectileClass();
	if (!World)
	{
		return false;
	}
	if (!SourceASC)
	{
		return false;
	}
	if (!ProjectileClass)
	{
		return false;
	}

	const FVector SpawnLocation = ArrowSpawnTransform.GetLocation();
	const FVector AimLocation = CachedEnemy->GetRangedAimLocation(CachedTarget);
	const FVector LaunchDirection = (AimLocation - SpawnLocation).GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		return false;
	}

	const FTransform SpawnTransform(LaunchDirection.Rotation(), SpawnLocation);
	AArrowProjectile* Projectile = World->SpawnActorDeferred<AArrowProjectile>(
		ProjectileClass,
		SpawnTransform,
		CachedEnemy,
		CachedEnemy,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return false;
	}

	Projectile->IgnoreActorForMovement(CachedEnemy);
	Projectile->IgnoreActorForMovement(Bow);

	TSubclassOf<UGameplayEffect> DamageEffectClass = Projectile->GetDirectDamageEffectClass();
	if (!DamageEffectClass)
	{
		DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	}

	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = SourceASC;
	DamageRequest.DamageEffectClass = DamageEffectClass;
	DamageRequest.AttackCoefficient = Projectile->GetAttackCoefficient();
	DamageRequest.ChargeMultiplier = 1.0f;
	DamageRequest.InstigatorActor = CachedEnemy;
	DamageRequest.EffectCauser = Projectile;
	DamageRequest.EffectLevel = Projectile->GetDirectDamageEffectLevel();
	Projectile->InitializeStrengthDamage(
		SourceASC,
		CachedEnemy,
		UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest));

	Projectile->SetOwner(CachedEnemy);
	Projectile->SetInstigator(CachedEnemy);
	Projectile->FinishSpawning(SpawnTransform);
	Projectile->LaunchArrow(LaunchDirection * Bow->GetProjectileSpeed());
	bProjectileFired = true;
	return true;
}

bool UGA_RangedEnemyAttack::PlayAttackMontage()
{
	UAnimMontage* Montage = CachedEnemy ? CachedEnemy->GetRangedAttackMontage() : nullptr;
	if (!Montage)
	{
		return false;
	}

	AttackMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("RangedEnemyAttackMontage")),
		Montage,
		CachedEnemy->GetRangedAttackMontagePlayRate(),
		NAME_None,
		true);
	if (!AttackMontageTask)
	{
		return false;
	}

	AttackMontageTask->OnCompleted.AddDynamic(this, &UGA_RangedEnemyAttack::OnAttackMontageCompleted);
	AttackMontageTask->OnBlendOut.AddDynamic(this, &UGA_RangedEnemyAttack::OnAttackMontageBlendOut);
	AttackMontageTask->OnInterrupted.AddDynamic(this, &UGA_RangedEnemyAttack::OnAttackMontageInterrupted);
	AttackMontageTask->OnCancelled.AddDynamic(this, &UGA_RangedEnemyAttack::OnAttackMontageCancelled);
	AttackMontageTask->ReadyForActivation();
	return true;
}

void UGA_RangedEnemyAttack::FinishAttack(bool bWasCancelled)
{
	if (bFinishingAttack)
	{
		return;
	}
	bFinishingAttack = true;

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_RangedEnemyAttack::AddAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
}

void UGA_RangedEnemyAttack::RemoveAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}
