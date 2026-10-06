#include "DeckAI/DeckEnemyCombatComponent.h"

#include "AIController.h"
#include "AI/BaseAIController.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BaseHealthComponent.h"
#include "BaseGameplayTags.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

UDeckEnemyCombatComponent::UDeckEnemyCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
ADeckEnemy* UDeckEnemyCombatComponent::GetEnemy() const { return Cast<ADeckEnemy>(GetOwner()); }

bool UDeckEnemyCombatComponent::FindAttackAbility(FGameplayAbilitySpecHandle& Out) const
{
	Out = FGameplayAbilitySpecHandle();
	const ADeckEnemy* Enemy = GetEnemy();
	const UBaseWeaponComponent* Weapon = Enemy ? Enemy->GetWeaponComponent() : nullptr;
	const UAbilitySystemComponent* ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	if (!ASC || !Weapon || !Weapon->IsWeaponEquipped()) return false;
	const FGameplayTag Tag = Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Ranged
		? GameplayAbility_RangedAttack : GameplayAbility_BasicAttack;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.SourceObject == Weapon->GetCurrentWeapon()
			&& Spec.Ability->GetAssetTags().HasTagExact(Tag))
		{
			Out = Spec.Handle;
			return true;
		}
	}
	return false;
}

bool UDeckEnemyCombatComponent::IsCoolingDown() const
{
	const ADeckEnemy* Enemy = GetEnemy();
	const UAbilitySystemComponent* ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	if (!Enemy || !ASC) return true;
	if (!Enemy->IsBalanceAttackReady() || ASC->HasMatchingGameplayTag(Cooldown_Enemy_BasicAttack)) return true;
	if (Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Ranged
		&& Enemy->GetRemainingAttackCooldown() > KINDA_SMALL_NUMBER) return true;
	FGameplayAbilitySpecHandle Handle;
	if (FindAttackAbility(Handle))
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		const FGameplayTagContainer* Tags = Spec && Spec->Ability ? Spec->Ability->GetCooldownTags() : nullptr;
		if (Tags && ASC->HasAnyMatchingGameplayTags(*Tags)) return true;
	}
	return false;
}

bool UDeckEnemyCombatComponent::HasClearAttackLine(AActor* Target) const
{
	const ADeckEnemy* Enemy = GetEnemy();
	if (!Enemy || !Enemy->IsValidCombatTarget(Target)) return false;
	if (Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Ranged) return Enemy->HasLineOfSightTo(Target);
	const UBaseWeaponComponent* Weapon = Enemy->GetWeaponComponent();
	const ABaseWeapon* Equipped = Weapon ? Weapon->GetCurrentWeapon() : nullptr;
	return Enemy->TraceLineOfSightFrom(Target, Equipped ? Equipped->GetActorLocation() : Enemy->GetActorLocation(),
		Enemy->GetRangedAimLocation(Target));
}

EDeckAttackOutcome UDeckEnemyCombatComponent::EvaluateAttack(AActor* Target, bool bCheckCooldown) const
{
	const ADeckEnemy* Enemy = GetEnemy();
	if (!Enemy || !Enemy->CanMoveOnDeck() || !Enemy->IsValidCombatTarget(Target)) return EDeckAttackOutcome::TargetInvalid;
	const ABaseAIController* Controller = Cast<ABaseAIController>(Enemy->GetController());
	if (bAttackCommitted || (Controller && Controller->HasDeferredDeckDecision())) return EDeckAttackOutcome::Interrupted;
	const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	const UBaseWeaponComponent* Weapon = Enemy->GetWeaponComponent();
	FGameplayAbilitySpecHandle Handle;
	if (!ASC || !Weapon || !Weapon->IsWeaponEquipped() || !FindAttackAbility(Handle)) return EDeckAttackOutcome::InvalidSetup;
	if (ASC->HasMatchingGameplayTag(State_Attacking) || ASC->HasMatchingGameplayTag(State_Damaged)) return EDeckAttackOutcome::Interrupted;
	if (bCheckCooldown && IsCoolingDown()) return EDeckAttackOutcome::Cooldown;
	const UDeckWalkAreaComponent* Area = Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent();
	FDeckWalkLocation Self, Other;
	if (!Area || !Area->ResolveActorOnDeck(*Enemy, Self) || !Area->ResolveActorOnDeck(*Target, Other)
		|| Self.SurfaceId != Other.SurfaceId) return EDeckAttackOutcome::OutOfRange;
	const float Distance = FVector::Distance(Enemy->GetActorLocation(), Target->GetActorLocation());
	const float Min = Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Melee ? 0.0f : Enemy->GetMinAttackRange();
	const float Max = Weapon->GetCurrentAttackRange();
	if (Max <= 0.0f || Distance < Min || Distance > Max) return EDeckAttackOutcome::OutOfRange;
	if (!HasClearAttackLine(Target)) return EDeckAttackOutcome::BlockedLOS;
	if (bCheckCooldown && Enemy->GetDeckCombatRole() == EDeckEnemyCombatRole::Melee
		&& !Enemy->HasBalancedMeleeAttackSlot()) return EDeckAttackOutcome::Cooldown;
	if (bCheckCooldown)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get())) return EDeckAttackOutcome::InvalidSetup;
	}
	return EDeckAttackOutcome::Ready;
}
bool UDeckEnemyCombatComponent::HasAttackPosition(AActor* Target) const
{
	return EvaluateAttack(Target, false) == EDeckAttackOutcome::Ready;
}
uint32 UDeckEnemyCombatComponent::BeginAttack(AActor* Target)
{
	if (!GetOwner()->HasAuthority() || bAttackCommitted) return 0;
	++AttemptId;
	if (AttemptId == 0) ++AttemptId;
	AttemptTarget = Target;
	LastOutcome = EDeckAttackOutcome::Ready;
	return AttemptId;
}
void UDeckEnemyCombatComponent::CommitAttack(uint32 Attempt, FGameplayAbilitySpecHandle Ability, UAnimMontage* Montage)
{
	if (!IsCurrentAttack(Attempt) || bAttackCommitted) return;
	bAttackCommitted = true; CommittedAbility = Ability; CommittedMontage = Montage;
	if (UAbilitySystemComponent* ASC = GetEnemy()->GetAbilitySystemComponent())
	{
		// Damage still applies. Only the ordinary reaction ability is blocked during this attack.
		ASC->BlockAbilitiesWithTags(FGameplayTagContainer(GameplayAbility_HitReaction));
		bBlocksHitReaction = true;
	}
}
bool UDeckEnemyCombatComponent::IsCurrentAttack(uint32 Attempt) const
{
	const ADeckEnemy* Enemy = GetEnemy();
	return Enemy && Enemy->CanMoveOnDeck() && Attempt != 0 && Attempt == AttemptId
		&& (!Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsDead());
}
bool UDeckEnemyCombatComponent::HasCommittedAttack() const
{
	return bAttackCommitted && IsCurrentAttack(AttemptId);
}
bool UDeckEnemyCombatComponent::IsAttackMontagePlaying() const
{
	const ADeckEnemy* Enemy = GetEnemy();
	const UAnimInstance* Anim = Enemy && Enemy->GetMesh() ? Enemy->GetMesh()->GetAnimInstance() : nullptr;
	return bAttackCommitted && CommittedMontage.IsValid() && Anim && Anim->Montage_IsActive(CommittedMontage.Get());
}
void UDeckEnemyCombatComponent::CancelCommittedAttack()
{
	if (ADeckEnemy* Enemy = GetEnemy(); Enemy && Enemy->HasAuthority() && CommittedAbility.IsValid())
		if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent()) ASC->CancelAbilityHandle(CommittedAbility);
}
void UDeckEnemyCombatComponent::ReleaseAttackCommitment()
{
	if (bBlocksHitReaction)
		if (ADeckEnemy* Enemy = GetEnemy())
			if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
				ASC->UnBlockAbilitiesWithTags(FGameplayTagContainer(GameplayAbility_HitReaction));
	bBlocksHitReaction = bAttackCommitted = false; CommittedAbility = FGameplayAbilitySpecHandle(); CommittedMontage.Reset();
}
void UDeckEnemyCombatComponent::RecordFailure(uint32 Attempt, EDeckAttackOutcome Outcome)
{
	if (GetOwner()->HasAuthority() && Attempt != 0 && Attempt == AttemptId
		&& LastOutcome != EDeckAttackOutcome::Executed && LastOutcome != EDeckAttackOutcome::BlockedLOS) LastOutcome = Outcome;
}
void UDeckEnemyCombatComponent::RecordExecuted(uint32 Attempt)
{
	if (GetOwner()->HasAuthority() && Attempt != 0 && Attempt == AttemptId)
	{
		LastOutcome = EDeckAttackOutcome::Executed;
		ClearRecovery();
	}
}
void UDeckEnemyCombatComponent::RecordBlockedLOS(uint32 Attempt, AActor* Target)
{
	ADeckEnemy* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority() || Attempt == 0 || Attempt != AttemptId
		|| Target != AttemptTarget.Get() || LastOutcome == EDeckAttackOutcome::Executed) return;
	LastOutcome = EDeckAttackOutcome::BlockedLOS;
	RecoveryShip = Enemy->GetDeckHostShip();
	const UDeckWalkAreaComponent* Area = RecoveryShip.IsValid() ? RecoveryShip->GetDeckWalkAreaComponent() : nullptr;
	if (!Area || !IsValid(Target) || !Area->ResolveActorOnDeck(*Target, RecoveryFloor)) { ClearRecovery(); return; }
	// Store actual feet, not the quantized graph node, and never follow hidden target motion.
	RecoveryFloor.LocalFloor = Area->ToLocal(Area->GetActorFeetWorld(*Target));
	RecoveryTarget = Target;
	RecoveryTime = GetWorld()->GetTimeSeconds();
}
void UDeckEnemyCombatComponent::EndAttack(uint32 Attempt, bool bCancelled)
{
	if (!GetOwner()->HasAuthority() || Attempt == 0 || Attempt != AttemptId) return;
	ReleaseAttackCommitment();
	// Give recovery its full movement window after the attack montage, even for long windups.
	if (LastOutcome == EDeckAttackOutcome::BlockedLOS && RecoveryTarget.IsValid()) RecoveryTime = GetWorld()->GetTimeSeconds();
	if (LastOutcome == EDeckAttackOutcome::Ready)
		LastOutcome = bCancelled ? EDeckAttackOutcome::Interrupted : EDeckAttackOutcome::InvalidSetup;
}
bool UDeckEnemyCombatComponent::HasRecovery(AActor* Target) const
{
	const ADeckEnemy* Enemy = GetEnemy();
	return Enemy && Enemy->CanMoveOnDeck() && RecoveryShip == Enemy->GetDeckHostShip()
		&& RecoveryTarget.IsValid() && (!Target || Target == RecoveryTarget.Get())
		&& Enemy->GetCombatTarget() == RecoveryTarget.Get()
		&& GetWorld()->GetTimeSeconds() - RecoveryTime <= 3.0;
}
bool UDeckEnemyCombatComponent::GetRecoveryGoal(FDeckWalkLocation& Out) const
{
	if (!RecoveryTarget.IsValid() || !RecoveryShip.IsValid()) return false;
	Out = RecoveryFloor;
	return true;
}
void UDeckEnemyCombatComponent::ClearRecovery()
{
	RecoveryTarget.Reset(); RecoveryShip.Reset(); RecoveryFloor = FDeckWalkLocation(); RecoveryTime = 0.0;
}
void UDeckEnemyCombatComponent::ResetCombat()
{
	if (!GetOwner()->HasAuthority()) return;
	ReleaseAttackCommitment();
	if (ADeckEnemy* Enemy = GetEnemy())
		if (ABaseAIController* Controller = Cast<ABaseAIController>(Enemy->GetController())) Controller->DiscardDeferredDeckDecision();
	++AttemptId; AttemptTarget.Reset(); ClearRecovery(); LastOutcome = EDeckAttackOutcome::InvalidSetup;
	if (FocusUsers > 0) { FocusUsers = 1; ReleaseFocus(); }
}
void UDeckEnemyCombatComponent::AcquireFocus()
{
	ADeckEnemy* Enemy = GetEnemy();
	AAIController* Controller = Enemy ? Cast<AAIController>(Enemy->GetController()) : nullptr;
	if (!Enemy || !Enemy->HasAuthority() || !Controller || !Enemy->GetCharacterMovement()) return;
	if (FocusUsers++ == 0)
	{
		FocusController = Controller;
		bOldControllerYaw = Enemy->bUseControllerRotationYaw;
		bOldOrientToMovement = Enemy->GetCharacterMovement()->bOrientRotationToMovement;
		bOldDesiredRotation = Enemy->GetCharacterMovement()->bUseControllerDesiredRotation;
		Enemy->bUseControllerRotationYaw = false;
		Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
		Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = true;
	}
	RefreshFocus();
}
void UDeckEnemyCombatComponent::RefreshFocus()
{
	ADeckEnemy* Enemy = GetEnemy();
	AAIController* Controller = FocusController.Get();
	if (FocusUsers > 0 && Enemy && Controller)
	{
		AActor* Target = Enemy->GetCombatTarget();
		if (Enemy->IsValidCombatTarget(Target)) { FocusTarget = Target; Controller->SetFocus(Target); }
		else if (Controller->GetFocusActor() == FocusTarget.Get()) Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
void UDeckEnemyCombatComponent::ReleaseFocus()
{
	if (FocusUsers <= 0 || --FocusUsers > 0) return;
	if (AAIController* Controller = FocusController.Get(); Controller && Controller->GetFocusActor() == FocusTarget.Get())
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	if (ADeckEnemy* Enemy = GetEnemy(); Enemy && Enemy->GetCharacterMovement())
	{
		Enemy->bUseControllerRotationYaw = bOldControllerYaw;
		Enemy->GetCharacterMovement()->bOrientRotationToMovement = bOldOrientToMovement;
		Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = bOldDesiredRotation;
	}
	FocusController.Reset(); FocusTarget.Reset();
}
