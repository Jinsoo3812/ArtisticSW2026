// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/BasePlayerAnimInstance.h"

#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UBasePlayerAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CacheOwningCharacter();
}

void UBasePlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	DeltaTime = DeltaSeconds;

	if (!CachedPawn || CachedPawn != TryGetPawnOwner())
	{
		CacheOwningCharacter();
	}

	if (!CachedCharacter)
	{
		ResetAnimationState();
		return;
	}

	if (CachedBasePlayer)
	{
		if (const ULocomotionAnimStateComponent* AnimState = CachedBasePlayer->GetAnimStateComponent())
		{
			UpdateFromAnimStateComponent(*AnimState);
		}
		else
		{
			UpdateFromPlayerCharacter(DeltaSeconds, *CachedBasePlayer);
		}
	}
	else
	{
		UpdateFromGenericCharacter(DeltaSeconds);
	}

	UpdateAimOffset();
}

void UBasePlayerAnimInstance::CacheOwningCharacter()
{
	CachedPawn = TryGetPawnOwner();
	CachedCharacter = Cast<ACharacter>(CachedPawn);
	CachedBasePlayer = Cast<ABasePlayer>(CachedPawn);
}

void UBasePlayerAnimInstance::MarkGroundStartFinished()
{
	if (CachedBasePlayer)
	{
		if (ULocomotionAnimStateComponent* AnimState = CachedBasePlayer->GetAnimStateComponent())
		{
			AnimState->MarkGroundStartFinished();
			UpdateFromAnimStateComponent(*AnimState);
		}
	}
}

void UBasePlayerAnimInstance::ResetAnimationState()
{
	GroundSpeed = 0.f;
	VerticalSpeed = 0.f;
	bIsInAir = false;
	bIsPhysicallyInAir = false;
	bIsJumping = false;
	bIsFallOffStart = false;
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = true;
	LastFallSpeed = 0.f;
	LandStartGroundSpeed = 0.f;
	LandStartFallSpeed = 0.f;
	bLandWasMoving = false;
	bLandWasSprinting = false;
	bUseHeavyLand = false;
	MoveInputSize = 0.f;
	MoveInputHeldTime = 0.f;
	CurrentStartToLoopDelay = 0.f;
	bHasMoveInput = false;
	bPrevHasMoveInput = false;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	bUseLoopDatabase = false;
	bUseSharpTurnDatabase = false;
	MoveInputTurnAngle = 0.f;
	bSharpTurnRequested = false;
	StopIntentSpeedThreshold = 80.f;
	IdleSpeedThreshold = 30.f;
	RunToSprintSpeedThreshold = 500.f;
	SharpTurnAngleThreshold = 60.f;
	MoveInputTurnDeadZoneAngle = 5.f;
	SharpTurnMinSpeed = 500.f;
	bIsSprinting = false;
	bIsCombatMode = false;
	bIsAttacking = false;
	bIsDodging = false;
	bIsHitReacting = false;
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	MovementDirection = 0.f;
	CombatInputForward = 0.f;
	CombatInputRight = 0.f;
	CombatForwardSpeed = 0.f;
	CombatRightSpeed = 0.f;
	AimYaw = 0.f;
	AimPitch = 0.f;
	AimOffsetAlpha = 0.f;
}

void UBasePlayerAnimInstance::UpdateFromGenericCharacter(float DeltaSeconds)
{
	const FVector Velocity = CachedCharacter->GetVelocity();
	VerticalSpeed = Velocity.Z;

	FVector HorizontalVelocity = Velocity;
	HorizontalVelocity.Z = 0.f;
	GroundSpeed = HorizontalVelocity.Size();

	const UCharacterMovementComponent* MovementComponent = CachedCharacter->GetCharacterMovement();
	bIsPhysicallyInAir = MovementComponent && MovementComponent->IsFalling();
	bIsInAir = bIsPhysicallyInAir;

	bHasMoveInput = GroundSpeed > GenericMoveInputSpeedThreshold;
	bPrevHasMoveInput = bHasMoveInput;
	MoveInputSize = bHasMoveInput ? 1.f : 0.f;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaSeconds : 0.f;
	CurrentStartToLoopDelay = 0.f;

	bIsJumping = false;
	bIsFallOffStart = false;
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = !bIsInAir;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	bUseLoopDatabase = false;
	bUseSharpTurnDatabase = false;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.f;
	StopIntentSpeedThreshold = 80.f;
	IdleSpeedThreshold = 30.f;
	RunToSprintSpeedThreshold = 500.f;
	SharpTurnAngleThreshold = 60.f;
	MoveInputTurnDeadZoneAngle = 5.f;
	SharpTurnMinSpeed = 500.f;
	bIsSprinting = false;
}

void UBasePlayerAnimInstance::UpdateFromPlayerCharacter(float DeltaSeconds, const ABasePlayer& PlayerCharacter)
{
	GroundSpeed = PlayerCharacter.GroundSpeed;
	VerticalSpeed = PlayerCharacter.VerticalSpeed;
	bIsInAir = PlayerCharacter.bIsInAir;
	bIsPhysicallyInAir = PlayerCharacter.bIsPhysicallyInAir;
	bIsJumping = PlayerCharacter.bIsJumping;
	bIsFallOffStart = PlayerCharacter.bIsFallOffStart;
	bIsLanding = PlayerCharacter.bIsLanding;
	bLandingRequested = PlayerCharacter.bLandingRequested;
	bCanEnterLand = PlayerCharacter.bCanEnterLand;
	bCanEnterGround = PlayerCharacter.bCanEnterGround;
	LastFallSpeed = PlayerCharacter.LastFallSpeed;
	LandStartGroundSpeed = PlayerCharacter.LandStartGroundSpeed;
	LandStartFallSpeed = PlayerCharacter.LandStartFallSpeed;
	bLandWasMoving = PlayerCharacter.bLandWasMoving;
	bLandWasSprinting = PlayerCharacter.bLandWasSprinting;
	bUseHeavyLand = PlayerCharacter.bUseHeavyLand;
	MoveInputSize = PlayerCharacter.MoveInputSize;
	MoveInputHeldTime = PlayerCharacter.MoveInputHeldTime;
	CurrentStartToLoopDelay = PlayerCharacter.CurrentStartToLoopDelay;
	bHasMoveInput = PlayerCharacter.bHasMoveInput;
	bPrevHasMoveInput = PlayerCharacter.bPrevHasMoveInput;
	bStartRequested = PlayerCharacter.bStartRequested;
	bStopRequested = PlayerCharacter.bStopRequested;
	bUseStartDatabase = PlayerCharacter.bUseStartDatabase;
	bGroundStartFinished = PlayerCharacter.bGroundStartFinished;
	bPendingGroundStartFinish = PlayerCharacter.bPendingGroundStartFinish;
	bStartWasSprinting = PlayerCharacter.bStartWasSprinting;
	bUseLoopDatabase = PlayerCharacter.bUseLoopDatabase;
	bUseSharpTurnDatabase = PlayerCharacter.bUseSharpTurnDatabase;
	MoveInputTurnAngle = PlayerCharacter.MoveInputTurnAngle;
	bSharpTurnRequested = PlayerCharacter.bSharpTurnRequested;
	bIsSprinting = PlayerCharacter.bIsSprinting;
	StopIntentSpeedThreshold = 80.f;
	IdleSpeedThreshold = 30.f;
	RunToSprintSpeedThreshold = 500.f;
	SharpTurnAngleThreshold = 60.f;
	MoveInputTurnDeadZoneAngle = 5.f;
	SharpTurnMinSpeed = 500.f;
	bIsCombatMode = PlayerCharacter.bIsCombatMode;
	bIsAttacking = PlayerCharacter.bIsAttacking;
	bIsDodging = PlayerCharacter.bIsDodging;
	bIsHitReacting = PlayerCharacter.bIsHitReacting;
	bIsPlayingCombatIntro = PlayerCharacter.bIsPlayingCombatIntro;
	bPendingCombatModeFromIntro = PlayerCharacter.bPendingCombatModeFromIntro;
	MovementDirection = PlayerCharacter.MovementDirection;
	CombatInputForward = PlayerCharacter.CombatInputForward;
	CombatInputRight = PlayerCharacter.CombatInputRight;
	CombatForwardSpeed = PlayerCharacter.CombatForwardSpeed;
	CombatRightSpeed = PlayerCharacter.CombatRightSpeed;
}

void UBasePlayerAnimInstance::UpdateFromAnimStateComponent(const ULocomotionAnimStateComponent& AnimState)
{
	GroundSpeed = AnimState.GroundSpeed;
	VerticalSpeed = AnimState.VerticalSpeed;
	bIsInAir = AnimState.bIsInAir;
	bIsPhysicallyInAir = AnimState.bIsPhysicallyInAir;
	bIsJumping = AnimState.bIsJumping;
	bIsFallOffStart = AnimState.bIsFallOffStart;
	bIsLanding = AnimState.bIsLanding;
	bLandingRequested = AnimState.bLandingRequested;
	bCanEnterLand = AnimState.bCanEnterLand;
	bCanEnterGround = AnimState.bCanEnterGround;
	LastFallSpeed = AnimState.LastFallSpeed;
	LandStartGroundSpeed = AnimState.LandStartGroundSpeed;
	LandStartFallSpeed = AnimState.LandStartFallSpeed;
	bLandWasMoving = AnimState.bLandWasMoving;
	bLandWasSprinting = AnimState.bLandWasSprinting;
	bUseHeavyLand = AnimState.bUseHeavyLand;
	MoveInputSize = AnimState.MoveInputSize;
	MoveInputHeldTime = AnimState.MoveInputHeldTime;
	CurrentStartToLoopDelay = AnimState.CurrentStartToLoopDelay;
	bHasMoveInput = AnimState.bHasMoveInput;
	bPrevHasMoveInput = AnimState.bPrevHasMoveInput;
	bStartRequested = AnimState.bStartRequested;
	bStopRequested = AnimState.bStopRequested;
	bUseStartDatabase = AnimState.bUseStartDatabase;
	bGroundStartFinished = AnimState.bGroundStartFinished;
	bPendingGroundStartFinish = AnimState.bPendingGroundStartFinish;
	bStartWasSprinting = AnimState.bStartWasSprinting;
	bUseLoopDatabase = AnimState.bUseLoopDatabase;
	bUseSharpTurnDatabase = AnimState.bUseSharpTurnDatabase;
	MoveInputTurnAngle = AnimState.MoveInputTurnAngle;
	bSharpTurnRequested = AnimState.bSharpTurnRequested;
	bIsSprinting = AnimState.bIsSprinting;
	StopIntentSpeedThreshold = AnimState.StopIntentSpeedThreshold;
	IdleSpeedThreshold = AnimState.IdleSpeedThreshold;
	RunToSprintSpeedThreshold = AnimState.RunToSprintSpeedThreshold;
	SharpTurnAngleThreshold = AnimState.SharpTurnAngleThreshold;
	MoveInputTurnDeadZoneAngle = AnimState.MoveInputTurnDeadZoneAngle;
	SharpTurnMinSpeed = AnimState.SharpTurnMinSpeed;
	MovementDirection = AnimState.MovementDirection;
	CombatInputForward = AnimState.CombatInputForward;
	CombatInputRight = AnimState.CombatInputRight;
	CombatForwardSpeed = AnimState.CombatForwardSpeed;
	CombatRightSpeed = AnimState.CombatRightSpeed;

	if (CachedBasePlayer)
	{
		bIsCombatMode = CachedBasePlayer->bIsCombatMode;
		bIsAttacking = CachedBasePlayer->bIsAttacking;
		bIsDodging = CachedBasePlayer->bIsDodging;
		bIsHitReacting = CachedBasePlayer->bIsHitReacting;
		bIsPlayingCombatIntro = CachedBasePlayer->bIsPlayingCombatIntro;
		bPendingCombatModeFromIntro = CachedBasePlayer->bPendingCombatModeFromIntro;
	}
}

void UBasePlayerAnimInstance::UpdateAimOffset()
{
	AimYaw = 0.f;
	AimPitch = 0.f;
	AimOffsetAlpha = 0.f;

	if (!CachedCharacter)
	{
		return;
	}

	const AController* Controller = CachedCharacter->GetController();
	if (!Controller)
	{
		return;
	}

	const FRotator ActorRotation = CachedCharacter->GetActorRotation();
	const FRotator ControlRotation = Controller->GetControlRotation();

	AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, ControlRotation.Yaw), -MaxAimYaw, MaxAimYaw);
	AimPitch = FMath::Clamp(FRotator::NormalizeAxis(ControlRotation.Pitch), -MaxAimPitch, MaxAimPitch);
	AimOffsetAlpha = CalculateAimOffsetAlpha();
}

float UBasePlayerAnimInstance::CalculateAimOffsetAlpha() const
{
	if (bForceAimOffsetAlwaysOn)
	{
		return 1.f;
	}

	if (bIsInAir || bIsLanding || bIsAttacking || bIsDodging || bIsHitReacting)
	{
		return 0.f;
	}

	if (bIsCombatMode)
	{
		return CombatAimAlpha;
	}

	if (bIsSprinting)
	{
		return SprintAimAlpha;
	}

	return GroundSpeed > GenericMoveInputSpeedThreshold ? MovingAimAlpha : StandingAimAlpha;
}
