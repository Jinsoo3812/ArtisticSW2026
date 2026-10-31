#include "Recovery/StuckRecoveryComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Recovery/StuckRecoveryCollision.h"
#include "SWCharacterMovementComponent.h"
#include "SwimmingComponent.h"

UStuckRecoveryComponent::UStuckRecoveryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
	SuppressionTags.AddTag(State_Dead);
	SuppressionTags.AddTag(State_Status_Stun);
	SuppressionTags.AddTag(State_Swimming);
}

void UStuckRecoveryComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	if (Character.IsValid())
	{
		CaptureLocationSample();
		Character->MovementModeChangedDelegate.AddDynamic(this, &ThisClass::HandleMovementModeChanged);
		AddTickPrerequisiteComponent(Character->GetCharacterMovement());
	}
	else SetComponentTickEnabled(false);
	if (!GetOwner()->HasAuthority()) SetComponentTickEnabled(false);
}

void UStuckRecoveryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Character.IsValid()) Character->MovementModeChangedDelegate.RemoveDynamic(this, &ThisClass::HandleMovementModeChanged);
	MonitoringSources.Empty();
	ResetHold();
	Super::EndPlay(Reason);
}

void UStuckRecoveryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UStuckRecoveryComponent, State, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UStuckRecoveryComponent, ServerHoldElapsed, COND_OwnerOnly);
}

FGuid UStuckRecoveryComponent::BeginMonitoring(UGameplayAbility* SourceAbility)
{
	if (!GetOwner()->HasAuthority()) return FGuid();
	const FGuid Id = FGuid::NewGuid();
	MonitoringSources.Add(Id, SourceAbility);
	return Id;
}

void UStuckRecoveryComponent::EndMonitoring(FGuid SourceId)
{
	if (!GetOwner()->HasAuthority() || MonitoringSources.Remove(SourceId) == 0) return;
	MonitoringUntil = FMath::Max(MonitoringUntil, GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, Detection.PostAbilityMonitorDuration));
}

void UStuckRecoveryComponent::RecordMovementInput(FVector2D Input)
{
	if (!FMath::IsFinite(Input.X) || !FMath::IsFinite(Input.Y)) return;
	MovementInput = Input.GetClampedToMaxSize(1.f);
}

void UStuckRecoveryComponent::SetDetectionSuspended(bool bSuspended)
{
	if (!GetOwner()->HasAuthority()) return;
	bDetectionSuspended = bSuspended;
	if (bSuspended) ResetDetection();
}

bool UStuckRecoveryComponent::CanMonitor() const
{
	if (!bEnableStuckDetection || bDetectionSuspended || !Character.IsValid()) return false;
	const ACharacter* OwnerCharacter = Character.Get();
	const APlayerController* Controller = Cast<APlayerController>(OwnerCharacter->GetController());
	const UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Controller || Controller->GetPawn() != OwnerCharacter || Controller->IsMoveInputIgnored()
		|| OwnerCharacter->IsMoveInputIgnored() || !Movement || !Movement->IsActive()
		|| !OwnerCharacter->GetCapsuleComponent()->IsQueryCollisionEnabled()
		|| OwnerCharacter->GetMesh()->IsSimulatingPhysics() || OwnerCharacter->IsActorBeingDestroyed()) return false;
	if (const ABasePlayer* Player = Cast<ABasePlayer>(OwnerCharacter))
	{
		if (!Player->CanMutateLifeGameplay() || Player->IsMountedForDamage()
			|| (Player->GetHealthComponent() && Player->GetHealthComponent()->IsDead())
			|| (Player->GetSwimmingComponent() && Player->GetSwimmingComponent()->IsCustomSwimming())) return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	return !Movement->IsSwimming() && (!ASC || !ASC->HasAnyMatchingGameplayTags(SuppressionTags));
}

void UStuckRecoveryComponent::HandleMovementModeChanged(ACharacter* InCharacter, EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (GetOwner()->HasAuthority())
		MonitoringUntil = FMath::Max(MonitoringUntil, GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, Detection.MovementModeMonitorDuration));
}

void UStuckRecoveryComponent::ObserveMontage()
{
	const UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	const bool bPlaying = Anim && Anim->IsAnyMontagePlaying();
	if (bPlaying || bMontageWasPlaying)
		MonitoringUntil = FMath::Max(MonitoringUntil, GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, Detection.PostAbilityMonitorDuration));
	bMontageWasPlaying = bPlaying;
}

void UStuckRecoveryComponent::CaptureLocationSample()
{
	PreviousLocation = Character->GetActorLocation();
	PreviousMovementBase = Character->GetMovementBase();
	if (PreviousMovementBase.IsValid())
		PreviousBaseLocalLocation = PreviousMovementBase->GetComponentTransform().InverseTransformPosition(PreviousLocation);
}

void UStuckRecoveryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!GetOwner()->HasAuthority() || !Character.IsValid()) return;
	if (!CanMonitor()) { ResetDetection(); return; }
	SampleElapsed += DeltaTime;
	if (SampleElapsed >= FMath::Max(0.02f, Detection.DetectionInterval))
	{
		const float Elapsed = SampleElapsed;
		SampleElapsed = 0.f;
		Evaluate(Elapsed);
	}
	if (State == EStuckRecoveryState::ConfirmedStuck && bServerInputHeld && !bHoldConsumed)
	{
		ServerHoldElapsed += DeltaTime;
		if (ServerHoldElapsed >= GetHoldDuration())
		{
			bHoldConsumed = true;
			ExecuteRecovery();
		}
	}
}

bool UStuckRecoveryComponent::HasAbnormalCollision() const
{
	return StuckRecoveryCollision::IsPenetrating(*Character, Detection.PenetrationInset)
		|| StuckRecoveryCollision::IsEnclosed(*Character, Detection);
}

void UStuckRecoveryComponent::Evaluate(float Elapsed)
{
	const FVector Location = Character->GetActorLocation();
	const FVector ExpectedPreviousLocation = PreviousMovementBase.IsValid()
		&& Character->GetMovementBase() == PreviousMovementBase.Get()
		? PreviousMovementBase->GetComponentTransform().TransformPosition(PreviousBaseLocalLocation) : PreviousLocation;
	const bool bStationary = FVector::Distance(Location, ExpectedPreviousLocation) <= FMath::Max(0.f, Detection.StationarySpeed) * Elapsed;
	CaptureLocationSample();
	// A long frame cannot substitute for multiple observations of a persistent fault.
	const float EvidenceElapsed = FMath::Min(Elapsed, FMath::Max(0.02f, Detection.DetectionInterval) * 2.f);
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bInput = MovementInput.SizeSquared() > 0.01f;
	const bool bRootIntent = (Movement->HasAnimRootMotion() && !Movement->RootMotionParams.GetRootMotionTransform().GetTranslation().IsNearlyZero())
		|| (Movement->HasRootMotionSources() && !Movement->Velocity.IsNearlyZero());
	const bool bIntent = bInput || bRootIntent || Movement->IsFalling();
	InactivityElapsed = bStationary && !bInput ? InactivityElapsed + EvidenceElapsed : 0.f;
	BlockedInputElapsed = bStationary && bIntent ? BlockedInputElapsed + EvidenceElapsed : 0.f;
	ObserveMontage();
	const float Now = GetWorld()->GetTimeSeconds();
	if (State == EStuckRecoveryState::Cooldown)
	{
		if (Now < CooldownUntil) return;
		ChangeState(EStuckRecoveryState::Idle);
	}
	const bool bEventMonitoring = MonitoringSources.Num() > 0 || Now < MonitoringUntil;
	const bool bMonitoring = bEventMonitoring
		|| InactivityElapsed >= FMath::Max(0.1f, Detection.FallbackInactivityDuration)
		|| BlockedInputElapsed >= FMath::Max(0.1f, Detection.BlockedInputDuration);
	if (!bMonitoring && State != EStuckRecoveryState::ConfirmedStuck && State != EStuckRecoveryState::SuspectedStuck)
	{
		ChangeState(EStuckRecoveryState::Idle);
		return;
	}
	if (!bEventMonitoring && BlockedInputElapsed < FMath::Max(0.1f, Detection.BlockedInputDuration)
		&& State != EStuckRecoveryState::SuspectedStuck && State != EStuckRecoveryState::ConfirmedStuck)
	{
		if (Now < NextFallbackProbeTime) return;
		NextFallbackProbeTime = Now + FMath::Max(0.1f, Detection.FallbackProbeInterval);
	}
	const bool bAbnormal = bStationary && (HasAbnormalCollision()
		|| (bInput && Movement->MovementMode == MOVE_None));
	if (!bAbnormal)
	{
		SuspectedElapsed = 0.f;
		ChangeState(bMonitoring ? EStuckRecoveryState::Monitoring : EStuckRecoveryState::Idle);
		return;
	}
	if (State == EStuckRecoveryState::ConfirmedStuck) return;
	SuspectedElapsed += EvidenceElapsed;
	ChangeState(SuspectedElapsed >= FMath::Max(0.1f, Detection.StuckConfirmDuration)
		? EStuckRecoveryState::ConfirmedStuck : EStuckRecoveryState::SuspectedStuck);
}

void UStuckRecoveryComponent::ChangeState(EStuckRecoveryState NewState)
{
	if (State == NewState) return;
	State = NewState;
	ResetHold();
	OnStateChanged.Broadcast(State);
	GetOwner()->ForceNetUpdate();
}

void UStuckRecoveryComponent::OnRep_State()
{
	// A held key cannot carry its progress into a new confirmed episode.
	bLocalInputHeld = false;
	OnStateChanged.Broadcast(State);
}

void UStuckRecoveryComponent::ResetHold()
{
	bServerInputHeld = bLocalInputHeld = false;
	ServerHoldElapsed = 0.f;
	bHoldConsumed = false;
}

void UStuckRecoveryComponent::ResetDetection()
{
	ResetHold();
	SampleElapsed = InactivityElapsed = BlockedInputElapsed = SuspectedElapsed = 0.f;
	NextFallbackProbeTime = 0.f;
	if (Character.IsValid()) CaptureLocationSample();
	ChangeState(EStuckRecoveryState::Idle);
}

void UStuckRecoveryComponent::SetRecoveryInputHeld(bool bHeld)
{
	if (!Character.IsValid() || !Character->IsLocallyControlled()) return;
	if (bHeld && !IsEscapeAvailable()) return;
	if (bHeld && bLocalInputHeld) return;
	bLocalInputHeld = bHeld;
	if (GetOwner()->HasAuthority()) ServerSetRecoveryInputHeld_Implementation(bHeld);
	else ServerSetRecoveryInputHeld(bHeld);
}

void UStuckRecoveryComponent::ServerSetRecoveryInputHeld_Implementation(bool bHeld)
{
	if (!bHeld) { ResetHold(); return; }
	if (!CanMonitor() || !IsEscapeAvailable() || bServerInputHeld || bHoldConsumed) return;
	bServerInputHeld = true;
	ServerHoldElapsed = 0.f;
}

float UStuckRecoveryComponent::GetHoldProgress() const
{
	return IsEscapeAvailable() && bLocalInputHeld
		? FMath::Clamp(ServerHoldElapsed / GetHoldDuration(), 0.f, 1.f) : 0.f;
}

void UStuckRecoveryComponent::ExecuteRecovery()
{
	// Refresh evidence at the moment of execution, including between scheduled samples.
	const UCharacterMovementComponent* CurrentMovement = Character->GetCharacterMovement();
	if (!CanMonitor() || !IsEscapeAvailable()
		|| (!HasAbnormalCollision() && !(MovementInput.SizeSquared() > 0.01f && CurrentMovement->MovementMode == MOVE_None)))
	{
		ResetDetection();
		return;
	}
	FVector Destination;
	bool bUncancelableSource = false;
	for (const auto& Entry : MonitoringSources)
	{
		if (Entry.Value.IsValid() && Entry.Value->IsActive() && !Entry.Value->CanBeCanceled())
		{
			bUncancelableSource = true;
			break;
		}
	}
	if (bUncancelableSource || !StuckRecoveryCollision::FindDestination(*Character, Escape, Destination))
	{
		ServerHoldElapsed = 0.f;
		// Keep the consumed latch: a failed hold requires release before retrying.
		ClientRecoveryResult(false);
		return;
	}
	ChangeState(EStuckRecoveryState::Recovering);
	TArray<TWeakObjectPtr<UGameplayAbility>> Abilities;
	MonitoringSources.GenerateValueArray(Abilities);
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		for (const TWeakObjectPtr<UGameplayAbility>& Ability : Abilities)
			if (Ability.IsValid() && Ability->IsActive()) ASC->CancelAbility(Ability.Get());
		// GAS also replicates the montage stop to the owning and observing clients.
		ASC->CurrentMontageStop(0.f);
	}
	if (UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance()) Anim->StopAllMontages(0.f);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->CurrentRootMotion.Clear();
	Movement->RootMotionParams.Clear();
	Movement->ClearAccumulatedForces();
	Movement->StopMovementImmediately();
	if (USWCharacterMovementComponent* SWMovement = Cast<USWCharacterMovementComponent>(Movement))
		SWMovement->EndHitReactionRootMotion();
	// Cancellation hooks may change collision or movement; validate again before teleporting.
	const bool bSucceeded = CanMonitor() && StuckRecoveryCollision::FindDestination(*Character, Escape, Destination)
		&& Character->TeleportTo(Destination, Character->GetActorRotation(), false, true);
	if (bSucceeded)
	{
		Character->SetBase(nullptr);
		Movement->SetMovementMode(MOVE_Falling);
		Movement->bJustTeleported = true;
		CaptureLocationSample();
		InactivityElapsed = BlockedInputElapsed = SuspectedElapsed = 0.f;
		MonitoringSources.Empty();
		CooldownUntil = GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, Escape.Cooldown);
		ChangeState(EStuckRecoveryState::Cooldown);
	}
	else
	{
		ChangeState(EStuckRecoveryState::ConfirmedStuck);
		bHoldConsumed = true;
	}
	GetOwner()->ForceNetUpdate();
	ClientRecoveryResult(bSucceeded);
}

void UStuckRecoveryComponent::ClientRecoveryResult_Implementation(bool bSucceeded)
{
	OnRecoveryResult.Broadcast(bSucceeded);
}
