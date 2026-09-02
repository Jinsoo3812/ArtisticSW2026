#include "GAS/Ability/GA_PlayerRoll.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"

UGA_PlayerRoll::UGA_PlayerRoll()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer RollAbilityTags;
	RollAbilityTags.AddTag(GameplayAbility_Player_Roll);
	RollAbilityTags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(RollAbilityTags);

	ActivationOwnedTags.AddTag(State_Rolling);
	ActivationBlockedTags.AddTag(State_Dead);
	ActivationBlockedTags.AddTag(State_Damaged);
	ActivationBlockedTags.AddTag(State_Rolling);

	// Existing offensive abilities use this tag, so the MVP cannot attack while
	// rolling. Hit reaction itself remains able to activate outside the i-frame.
	BlockAbilitiesWithTag.AddTag(GameplayAbility_InterruptibleByHit);
}

bool UGA_PlayerRoll::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return Movement && (!bRequireGrounded || Movement->IsMovingOnGround());
}

void UGA_PlayerRoll::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bRollFinished = false;
	bInvulnerabilityActive = false;
	bRecoveryRequested = false;

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishRoll(true);
		return;
	}

	CurrentRollIntent = BuildRollIntent();
	K2_OnRollIntentResolved(CurrentRollIntent);
	StartRollEventListeners();

	if (!StartRollExecution(CurrentRollIntent))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PlayerRollGA: Roll executor failed to start. Avatar=%s Montage=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GetNameSafe(RollMontage));
		FinishRoll(true);
	}
}

void UGA_PlayerRoll::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// External ASC cancellation does not pass through FinishRoll, so normalize it
	// into the same public completion contract before cleanup.
	if (!bRollFinished)
	{
		bRollFinished = true;
		K2_OnRollFinished(bWasCancelled);
	}

	RemoveRollInvulnerability();
	StopRollExecution(bWasCancelled);

	RollMontageTask = nullptr;
	InvulnerabilityBeginTask = nullptr;
	InvulnerabilityEndTask = nullptr;
	RollRecoveryTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

FRollIntent UGA_PlayerRoll::BuildRollIntent() const
{
	FRollIntent Intent;
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return Intent;
	}

	FVector WorldDirection = FVector::ZeroVector;
	bool bHasInput = false;

	// 1. Direct input from BasePlayer's LocomotionAnimStateComponent (WASD Enhanced Input)
	if (const ABasePlayer* BasePlayer = Cast<ABasePlayer>(Character))
	{
		if (const ULocomotionAnimStateComponent* AnimState = BasePlayer->GetAnimStateComponent())
		{
			const FVector2D MoveInput = AnimState->CachedMoveInput;
			if (MoveInput.SizeSquared() > 0.01f)
			{
				const AController* Controller = Character->GetController();
				const FRotator ControlRot = Controller ? Controller->GetControlRotation() : Character->GetActorRotation();
				const FRotator YawRot(0.0f, ControlRot.Yaw, 0.0f);
				const FVector CamForward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
				const FVector CamRight = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

				WorldDirection = (CamForward * MoveInput.Y + CamRight * MoveInput.X).GetSafeNormal();
				bHasInput = true;
				Intent.InputMagnitude = FMath::Clamp(MoveInput.Size(), 0.0f, 1.0f);
			}
		}
	}

	// 2. Pending or last movement input vector
	if (!bHasInput)
	{
		FVector MovementInput = Character->GetPendingMovementInputVector();
		if (MovementInput.IsNearlyZero())
		{
			MovementInput = Character->GetLastMovementInputVector();
		}
		MovementInput.Z = 0.0f;
		if (!MovementInput.IsNearlyZero())
		{
			WorldDirection = MovementInput.GetSafeNormal();
			bHasInput = true;
			Intent.InputMagnitude = FMath::Clamp(MovementInput.Size(), 0.0f, 1.0f);
		}
	}

	// 3. Fallback: Velocity if actively moving
	if (!bHasInput)
	{
		FVector Velocity = Character->GetVelocity();
		Velocity.Z = 0.0f;
		if (Velocity.SizeSquared() > 100.0f)
		{
			WorldDirection = Velocity.GetSafeNormal();
			bHasInput = true;
			Intent.InputMagnitude = 1.0f;
		}
	}

	// 4. Default to character forward if completely stationary with no input
	if (!bHasInput)
	{
		FVector Forward = Character->GetActorForwardVector();
		Forward.Z = 0.0f;
		WorldDirection = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		Intent.InputMagnitude = 0.0f;
	}

	Intent.bHasMovementInput = bHasInput;
	Intent.WorldDirection = WorldDirection;
	Intent.LocalDirection = Character->GetActorTransform()
		.InverseTransformVectorNoScale(Intent.WorldDirection)
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	Intent.InitialVelocity = Character->GetVelocity();
	Intent.InitialVelocity.Z = 0.0f;
	Intent.RequestedFacingRotation = Intent.WorldDirection.Rotation();
	Intent.RequestedFacingRotation.Pitch = 0.0f;
	Intent.RequestedFacingRotation.Roll = 0.0f;
	return Intent;
}

bool UGA_PlayerRoll::StartRollExecution(const FRollIntent& RollIntent)
{
	if (!RollMontage)
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance && Character->GetNetMode() != NM_DedicatedServer)
	{
		return false;
	}

	if (bRotateToIntent)
	{
		if (ABasePlayer* BasePlayer = Cast<ABasePlayer>(Character))
		{
			BasePlayer->bUseControllerRotationYaw = false;
			BasePlayer->bIsDodging = true;
		}
		if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
		{
			CMC->bUseControllerDesiredRotation = false;
			CMC->bOrientRotationToMovement = false;
		}
		Character->SetActorRotation(RollIntent.RequestedFacingRotation);
	}

	RollMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("RollMontageTask")),
		RollMontage,
		FMath::Max(RollMontagePlayRate, 0.01f),
		RollMontageStartSection,
		false); // Recovery/cancellation owns the blend-out policy explicitly.

	if (!RollMontageTask)
	{
		return false;
	}

	RollMontageTask->OnCompleted.AddDynamic(this, &UGA_PlayerRoll::HandleRollMontageCompleted);
	RollMontageTask->OnInterrupted.AddDynamic(this, &UGA_PlayerRoll::HandleRollMontageInterrupted);
	RollMontageTask->OnCancelled.AddDynamic(this, &UGA_PlayerRoll::HandleRollMontageCancelled);
	RollMontageTask->ReadyForActivation();
	return true;
}

void UGA_PlayerRoll::StopRollExecution(bool bWasCancelled)
{
	// A recovery event starts its blend before normal ability completion. Forced
	// cancellation still needs an explicit blend when normal-end stopping is off.
	if (bWasCancelled)
	{
		BeginRecoveryBlendOut();
	}
	// Motion Matching implementations override this hook to clear their
	// request/trajectory state.
}

void UGA_PlayerRoll::FinishRoll(bool bWasCancelled)
{
	if (bRollFinished)
	{
		return;
	}

	bRollFinished = true;
	K2_OnRollFinished(bWasCancelled);

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_PlayerRoll::HandleInvulnerabilityBegin(FGameplayEventData Payload)
{
	AddRollInvulnerability();
}

void UGA_PlayerRoll::HandleInvulnerabilityEnd(FGameplayEventData Payload)
{
	RemoveRollInvulnerability();
}

void UGA_PlayerRoll::HandleRollRecovery(FGameplayEventData Payload)
{
	// Blend the montage out first, then release State.Rolling and ability blocks.
	// Locomotion can respond immediately while the remaining pose fades naturally.
	bRecoveryRequested = true;
	BeginRecoveryBlendOut();
	FinishRoll(false);
}

void UGA_PlayerRoll::HandleRollMontageCompleted()
{
	FinishRoll(false);
}

void UGA_PlayerRoll::HandleRollMontageInterrupted()
{
	// Montage_Stop reports an interruption even when it was deliberately invoked
	// by the authored recovery point. Preserve that path as a successful roll.
	FinishRoll(!bRecoveryRequested);
}

void UGA_PlayerRoll::HandleRollMontageCancelled()
{
	FinishRoll(true);
}

void UGA_PlayerRoll::StartRollEventListeners()
{
	InvulnerabilityBeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Ability_Roll_Invulnerability_Begin,
		nullptr,
		false,
		true);
	InvulnerabilityEndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Ability_Roll_Invulnerability_End,
		nullptr,
		false,
		true);
	RollRecoveryTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Ability_Roll_Recovery,
		nullptr,
		true,
		true);

	if (InvulnerabilityBeginTask)
	{
		InvulnerabilityBeginTask->EventReceived.AddDynamic(
			this,
			&UGA_PlayerRoll::HandleInvulnerabilityBegin);
		InvulnerabilityBeginTask->ReadyForActivation();
	}

	if (InvulnerabilityEndTask)
	{
		InvulnerabilityEndTask->EventReceived.AddDynamic(
			this,
			&UGA_PlayerRoll::HandleInvulnerabilityEnd);
		InvulnerabilityEndTask->ReadyForActivation();
	}

	if (RollRecoveryTask)
	{
		RollRecoveryTask->EventReceived.AddDynamic(
			this,
			&UGA_PlayerRoll::HandleRollRecovery);
		RollRecoveryTask->ReadyForActivation();
	}
}

void UGA_PlayerRoll::BeginRecoveryBlendOut()
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (AnimInstance && RollMontage && AnimInstance->Montage_IsPlaying(RollMontage))
	{
		AnimInstance->Montage_Stop(FMath::Max(0.0f, RollRecoveryBlendOutTime), RollMontage);
	}
}

void UGA_PlayerRoll::AddRollInvulnerability()
{
	if (bInvulnerabilityActive)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Invulnerable);
		bInvulnerabilityActive = true;
	}
}

void UGA_PlayerRoll::RemoveRollInvulnerability()
{
	if (!bInvulnerabilityActive)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Invulnerable);
	}
	bInvulnerabilityActive = false;
}
