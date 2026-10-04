#include "GAS/Ability/GA_RangedEnemyAttack.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "BaseGameplayTags.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "GASCombatLibrary.h"
#include "RangedEnemy/RangedEnemy.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "RangedEnemy/EnemyBowShotPreparation.h"
#include "RangedEnemy/RangedEnemyProjectile.h"
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

bool UGA_RangedEnemyAttack::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const ADeckEnemy* Deck = ActorInfo ? Cast<ADeckEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	const ABaseAIController* AI = Deck ? Cast<ABaseAIController>(Deck->GetController()) : nullptr;
	if (Deck && (Deck->GetDeckCombatComponent()->HasCommittedAttack() || (AI && AI->HasDeferredDeckDecision()))) return false;
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
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
	DeckCombat.Reset(); DeckAttackAttempt = 0;
	if (ADeckEnemy* Deck = Cast<ADeckEnemy>(CachedEnemy))
	{
		DeckCombat = Deck->GetDeckCombatComponent();
		DeckAttackAttempt = DeckCombat->BeginAttack(CachedTarget);
	}
	bProjectileFired = false;
	bFireEventReceived = false;
	bFinishingAttack = false;
	bOwnsServerPoseRefresh = false;
	bShotQueued = false;
	bMontageCompleted = false;
	PendingShotId = FGuid::NewGuid();

	if (!CachedEnemy || !CachedEnemy->IsBalanceAttackReady())
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::Cooldown);
		FinishAttack(true);
		return;
	}

	if (!CachedEnemy->HasAuthority())
	{
		FinishAttack(true);
		return;
	}
	if (!CachedEnemy->CanAttackCurrentTarget(false))
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt,
			CachedEnemy->IsValidCombatTarget(CachedTarget) ? EDeckAttackOutcome::OutOfRange : EDeckAttackOutcome::TargetInvalid);
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

	UAnimMontage* AttackMontage = CachedEnemy->GetRangedAttackMontage();
	const FGameplayTag FireEventTag = CachedEnemy->GetRangedFireEventTag();
	if (!AttackMontage || !FireEventTag.IsValid())
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::InvalidSetup);
		UE_LOG(LogTemp, Warning,
			TEXT("Ranged attack requires DA_Weapon AttackMontage and a valid FireEventTag. Enemy=%s Montage=%s Tag=%s"),
			*GetNameSafe(CachedEnemy), *GetNameSafe(AttackMontage), *FireEventTag.ToString());
		FinishAttack(true);
		return;
	}
	if (DeckCombat.IsValid()) DeckCombat->CommitAttack(DeckAttackAttempt, Handle, AttackMontage);
	AddAttackStateTag();

	FireProjectileEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		FireEventTag,
		nullptr,
		false,
		true);
	if (!FireProjectileEventTask)
	{
		FinishAttack(true);
		return;
	}
	FireProjectileEventTask->EventReceived.AddDynamic(this, &UGA_RangedEnemyAttack::OnFireProjectileEvent);
	FireProjectileEventTask->ReadyForActivation();

	if (!PlayAttackMontage())
	{
		FinishAttack(true);
	}
}

void UGA_RangedEnemyAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ShotComponent.IsValid()) ShotComponent->Cancel(PendingShotId);
	PendingShotId.Invalidate();
	bShotQueued = false;
	bFinishingAttack = true;
	if (DeckCombat.IsValid()) DeckCombat->EndAttack(DeckAttackAttempt, bWasCancelled);
	DeckCombat.Reset(); DeckAttackAttempt = 0;
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
	bOwnsServerPoseRefresh = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	bFinishingAttack = false;
}

void UGA_RangedEnemyAttack::OnFireProjectileEvent(FGameplayEventData Payload)
{
	if (bProjectileFired || bFinishingAttack || bShotQueued || !IsActive() || (DeckCombat.IsValid() && bFireEventReceived))
	{
		return;
	}
	bFireEventReceived = true;

	ShotComponent = UProjectileShotComponent::FindOrAdd(CachedEnemy);
	if (!ShotComponent.IsValid()) { HandleShotFailure(); return; }
	bShotQueued = ShotComponent->Queue(this, PendingShotId,
		FProjectileShotCommitDelegate::CreateUObject(this, &UGA_RangedEnemyAttack::CommitProjectile),
		FProjectileShotFinishedDelegate::CreateUObject(this, &UGA_RangedEnemyAttack::OnShotCommitted));
	if (!bShotQueued) HandleShotFailure();
}

void UGA_RangedEnemyAttack::HandleShotFailure()
{
	if (DeckCombat.IsValid())
	{
		if (DeckCombat->GetLastOutcome() == EDeckAttackOutcome::Ready)
			DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::InvalidSetup);
		if (bMontageCompleted) FinishAttack(false);
	}
	else FinishAttack(true);
}

EProjectileShotCommit UGA_RangedEnemyAttack::CommitProjectile()
{
	return FireProjectile() ? EProjectileShotCommit::Succeeded : EProjectileShotCommit::Rejected;
}

void UGA_RangedEnemyAttack::OnShotCommitted(bool bSucceeded)
{
	bShotQueued = false;
	if (DeckCombat.IsValid())
	{
		if (!bSucceeded) HandleShotFailure();
		else if (bMontageCompleted) FinishAttack(false);
	}
	else if (!bSucceeded || bMontageCompleted) FinishAttack(!bSucceeded);
}

void UGA_RangedEnemyAttack::OnAttackMontageCompleted()
{
	bMontageCompleted = true;
	if (bShotQueued) return;
	if (!bProjectileFired && !bFireEventReceived)
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::InvalidSetup);
		UE_LOG(LogTemp, Warning,
			TEXT("Ranged attack montage completed without FireArrow notify. Enemy=%s Montage=%s"),
			*GetNameSafe(CachedEnemy),
			*GetNameSafe(CachedEnemy ? CachedEnemy->GetRangedAttackMontage() : nullptr));
	}
	FinishAttack(DeckCombat.IsValid() ? false : !bProjectileFired);
}

void UGA_RangedEnemyAttack::OnAttackMontageBlendOut()
{
	// BlendOut is the start of the tail. Keep the GA and BT task alive until OnCompleted.
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
	if (DeckCombat.IsValid() && !DeckCombat->IsCurrentAttack(DeckAttackAttempt)) return false;
	if (!CachedEnemy || !CachedEnemy->HasAuthority() || !IsActive() || bFinishingAttack)
	{
		return false;
	}
	if (bProjectileFired)
	{
		return false;
	}
	if (!CachedTarget)
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::TargetInvalid);
		return false;
	}
	if (CachedEnemy->GetCombatTarget() != CachedTarget)
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt, EDeckAttackOutcome::TargetInvalid);
		return false;
	}
	AEnemyBow* Bow = CachedEnemy->GetEquippedBow();
	if (!Bow)
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
	if (!ProjectileClass || !ProjectileClass->IsChildOf(ARangedEnemyProjectile::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnemyBow] ProjectileClass must derive from RangedEnemyProjectile. Bow=%s Class=%s"),
			*GetNameSafe(Bow), *GetNameSafe(ProjectileClass.Get()));
		return false;
	}

	// Read current world-space socket and target after based movement, in the late commit.
	FTransform ArrowSpawnTransform;
	FVector AimLocation;
	const ERangedShotSnapshotResult SnapshotResult = CachedEnemy->CaptureRangedAim(
		CachedTarget,
		ArrowSpawnTransform,
		AimLocation, !DeckCombat.IsValid());
	if (SnapshotResult != ERangedShotSnapshotResult::Ready)
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordFailure(DeckAttackAttempt,
			SnapshotResult == ERangedShotSnapshotResult::MissingAttackOrigin ? EDeckAttackOutcome::InvalidSetup : EDeckAttackOutcome::TargetInvalid);
		return false;
	}

	FProjectileShotInput Input;
	Input.ShotId = PendingShotId;
	Input.MuzzleTransform = ArrowSpawnTransform;
	Input.AimPoint = AimLocation;
	Input.AimDirection = (AimLocation - ArrowSpawnTransform.GetLocation()).GetSafeNormal();
	Input.AimServerTime = ProjectileShotPreparation::GetServerTime(World);
	Input.Speed = Bow->GetProjectileSpeed();
	Input.GravityZ = World->GetGravityZ() * EnemyBowShotPreparation::GetGravityScale(
		ProjectileClass.GetDefaultObject()->GetFlightGravityScale());
	FProjectileShotSnapshot Shot;
	if (!EnemyBowShotPreparation::Prepare(CachedEnemy, CachedTarget, Input, Shot))
	{
		return false;
	}
	if (!CachedEnemy->HasClearRangedLaunch(CachedTarget, Shot))
	{
		if (DeckCombat.IsValid()) DeckCombat->RecordBlockedLOS(DeckAttackAttempt, CachedTarget);
		else CachedEnemy->HandleRangedReleaseLineOfSightBlocked(CachedTarget);
		return false;
	}

	const FTransform& SpawnTransform = Shot.SpawnTransform;
	ARangedEnemyProjectile* Projectile = World->SpawnActorDeferred<ARangedEnemyProjectile>(
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
	Projectile->FinishSpawning(SpawnTransform);

	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = SourceASC;

	DamageRequest.AttackCoefficient = Projectile->GetAttackCoefficient();
	DamageRequest.ChargeMultiplier = 1.0f;
	DamageRequest.InstigatorActor = CachedEnemy;
	DamageRequest.EffectCauser = Projectile;
	DamageRequest.EffectLevel = Projectile->GetDirectDamageEffectLevel();
	const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest);
	if (!DamageSpec.IsValid()) { Projectile->Destroy(); return false; }
	if (!Projectile->InitializeStrengthDamage(SourceASC, CachedEnemy, DamageSpec)) { Projectile->Destroy(); return false; }

	Projectile->SetOwner(CachedEnemy);
	Projectile->SetInstigator(CachedEnemy);
	if (!Projectile->LaunchEnemyShot(Shot, Bow)) { Projectile->Destroy(); return false; }
	bProjectileFired = true;
	if (DeckCombat.IsValid()) DeckCombat->RecordExecuted(DeckAttackAttempt);
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
		true, 1.f, 0.f, true);
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
