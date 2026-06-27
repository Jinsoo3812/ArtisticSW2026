#include "Animation/BasePlayerAnimStateComponent.h"

#include "BasePlayer.h"
#include "GameFramework/CharacterMovementComponent.h"

UBasePlayerAnimStateComponent::UBasePlayerAnimStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBasePlayerAnimStateComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheOwner();
}

void UBasePlayerAnimStateComponent::CacheOwner()
{
	CachedBasePlayer = Cast<ABasePlayer>(GetOwner());
	if (!CachedBasePlayer)
	{
		return;
	}

	bIsSprinting = (CachedBasePlayer->GetAnimStateComponent() && CachedBasePlayer->GetAnimStateComponent()->bIsSprinting);
}

void UBasePlayerAnimStateComponent::UpdateAnimationState(float DeltaTime)
{
	if (!CachedBasePlayer)
	{
		CacheOwner();
	}

	if (!CachedBasePlayer)
	{
		return;
	}

	UpdateAirState(DeltaTime);
	UpdateMovementRequestState(DeltaTime);
	UpdateMaxWalkSpeed();
	UpdateCombatMovementState();
}

void UBasePlayerAnimStateComponent::SetMoveInput(float Right, float Forward)
{
	CachedMoveInput = FVector2D(Right, Forward);
}

void UBasePlayerAnimStateComponent::ClearMoveInput()
{
	CachedMoveInput = FVector2D::ZeroVector;
}

bool UBasePlayerAnimStateComponent::IsInAirForAnimation() const
{
	const UCharacterMovementComponent* MovementComponent = CachedBasePlayer ? CachedBasePlayer->GetCharacterMovement() : nullptr;
	return MovementComponent && MovementComponent->MovementMode == MOVE_Falling;
}

bool UBasePlayerAnimStateComponent::ShouldUseLocalInput() const
{
	return CachedBasePlayer && CachedBasePlayer->IsLocallyControlled();
}

FVector2D UBasePlayerAnimStateComponent::GetMovementInputForState() const
{
	if (!CachedBasePlayer)
	{
		return FVector2D::ZeroVector;
	}

	if (ShouldUseLocalInput())
	{
		return CachedMoveInput.GetClampedToMaxSize(1.f);
	}

	FVector HorizontalVelocity = CachedBasePlayer->GetVelocity();
	HorizontalVelocity.Z = 0.f;
	if (HorizontalVelocity.SizeSquared() <= FMath::Square(GenericMoveInputSpeedThreshold))
	{
		return FVector2D::ZeroVector;
	}

	const FVector LocalVelocity = CachedBasePlayer->GetActorTransform().InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
	return FVector2D(LocalVelocity.Y, LocalVelocity.X).GetClampedToMaxSize(1.f);
}

void UBasePlayerAnimStateComponent::UpdateAirState(float DeltaTime)
{
	const FVector Velocity = CachedBasePlayer->GetVelocity();
	VerticalSpeed = Velocity.Z;

	FVector HorizontalVelocity = Velocity;
	HorizontalVelocity.Z = 0.f;
	GroundSpeed = HorizontalVelocity.Size();

	if (VerticalSpeed < 0.f)
	{
		LastFallSpeed = FMath::Abs(VerticalSpeed);
	}

	const bool bNowInAir = IsInAirForAnimation();
	bIsPhysicallyInAir = bNowInAir;

	if (bWasInAir && !bNowInAir && !bIsLanding && !bLandingRequested)
	{
		StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed)), false);
	}
	else if (!bWasInAir
		&& bNowInAir
		&& !bIsJumping
		&& !bIsLanding
		&& !bSuppressFallOffStart)
	{
		StartFallOffStart();
	}

	if (bNowInAir)
	{
		bIsInAir = true;
	}
	else if (!bIsJumping && !bLandingRequested && !bIsLanding)
	{
		bIsInAir = false;
		bSuppressFallOffStart = false;
	}

	bWasInAir = bNowInAir;
	bCanEnterLand = bLandingRequested;
	bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;
}

void UBasePlayerAnimStateComponent::UpdateMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bHasMoveInput;

	const FVector2D MoveInput = GetMovementInputForState();

	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone || (!ShouldUseLocalInput() && GroundSpeed > GenericMoveInputSpeedThreshold);
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.f;

	MoveInputTurnAngle = 0.f;
	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	bUseLoopDatabase = false;
	bUseSharpTurnDatabase = false;

	if (bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone && MoveInput.Size() > MoveInputDeadZone)
	{
		const FVector2D PrevDir = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrDir = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PrevDir, CurrDir), -1.f, 1.f);
		const float Cross = PrevDir.Y * CurrDir.X - PrevDir.X * CurrDir.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
		if (FMath::Abs(MoveInputTurnAngle) < MoveInputTurnDeadZoneAngle)
		{
			MoveInputTurnAngle = 0.f;
		}
	}

	const bool bCanRequestGroundMove = !bIsInAir && !bIsLanding && !bIsJumping && !bIsFallOffStart;
	if (!bCanRequestGroundMove)
	{
		ClearMovementRequests();
		PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
		return;
	}

	const bool bJustStartedMoving = !bPrevHasMoveInput && bHasMoveInput;
	const bool bJustStoppedMoving = bPrevHasMoveInput && !bHasMoveInput;

	if (bJustStartedMoving)
	{
		MoveInputHeldTime = 0.f;
		bGroundStartFinished = !ShouldUseLocalInput();
		bPendingGroundStartFinish = false;
		bStartWasSprinting = bIsSprinting;
	}

	if (!bHasMoveInput)
	{
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bStartWasSprinting = false;
	}

	if (bPendingGroundStartFinish && MoveInputHeldTime >= MinStartDatabaseTime)
	{
		bGroundStartFinished = true;
		bPendingGroundStartFinish = false;
	}

	bSharpTurnRequested =
		bIsSprinting &&
		bHasMoveInput &&
		bPrevHasMoveInput &&
		GroundSpeed >= SharpTurnMinSpeed &&
		FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;

	bStartRequested = ShouldUseLocalInput() && bJustStartedMoving;
	bStopRequested = bJustStoppedMoving && GroundSpeed > StopIntentSpeedThreshold;
	CurrentStartToLoopDelay = bIsSprinting ? SprintStartToLoopDelay : StartToLoopDelay;
	bUseStartDatabase = ShouldUseLocalInput() && bHasMoveInput && !bGroundStartFinished && MoveInputHeldTime < CurrentStartToLoopDelay;
	bUseSharpTurnDatabase = ShouldUseLocalInput() && bSharpTurnRequested;
	bUseLoopDatabase = bHasMoveInput && !bUseStartDatabase && !bUseSharpTurnDatabase && !bStopRequested;

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
}

void UBasePlayerAnimStateComponent::UpdateCombatMovementState()
{
	const FVector2D CombatMoveInput = GetMovementInputForState();
	CombatInputRight = CombatMoveInput.X;
	CombatInputForward = CombatMoveInput.Y;

	if (CachedBasePlayer->bIsCombatMode && bHasMoveInput)
	{
		const float CurrentMaxSpeed = CachedBasePlayer->GetCharacterMovement() ? CachedBasePlayer->GetCharacterMovement()->MaxWalkSpeed : WalkSpeed;
		CombatRightSpeed = CombatInputRight * CurrentMaxSpeed;
		CombatForwardSpeed = CombatInputForward * CurrentMaxSpeed;
		MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatInputRight, CombatInputForward));
	}
	else
	{
		MovementDirection = 0.f;
		CombatForwardSpeed = 0.f;
		CombatRightSpeed = 0.f;
	}
}

void UBasePlayerAnimStateComponent::UpdateMaxWalkSpeed() const
{
	if (!ShouldUseLocalInput())
	{
		return;
	}

	UCharacterMovementComponent* MovementComponent = CachedBasePlayer ? CachedBasePlayer->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return;
	}

	const bool bCanSprint =
		bIsSprinting &&
		!CachedBasePlayer->bIsAttacking &&
		!CachedBasePlayer->bIsDodging &&
		!CachedBasePlayer->bIsHitReacting;

	MovementComponent->MaxWalkSpeed = bCanSprint ? SprintSpeed : WalkSpeed;
	MovementComponent->RotationRate = FRotator(
		0.f,
		bCanSprint ? SprintRotationRateYaw : WalkRotationRateYaw,
		0.f
	);
}

void UBasePlayerAnimStateComponent::ClearMovementRequests()
{
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	bUseLoopDatabase = false;
	bUseSharpTurnDatabase = false;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	MoveInputHeldTime = 0.f;
	CurrentStartToLoopDelay = 0.f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.f;
}

void UBasePlayerAnimStateComponent::MarkGroundStartFinished()
{
	if (MoveInputHeldTime < MinStartDatabaseTime)
	{
		bPendingGroundStartFinish = true;
		return;
	}

	bPendingGroundStartFinish = false;
	bGroundStartFinished = true;
	bUseStartDatabase = false;
	bUseLoopDatabase = bHasMoveInput && !bSharpTurnRequested && !bStopRequested;
}

void UBasePlayerAnimStateComponent::HandleJumpStarted()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
	World->GetTimerManager().ClearTimer(LandingTimerHandle);
	World->GetTimerManager().ClearTimer(LandingRequestTimerHandle);
	StopFallOffStart();

	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIsInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;
	bIsJumping = true;

	World->GetTimerManager().SetTimer(
		JumpStartTimerHandle,
		this,
		&UBasePlayerAnimStateComponent::FinishJumpStart,
		FMath::Max(0.1f, JumpStartMaxDuration),
		false
	);
}

void UBasePlayerAnimStateComponent::FinishJumpStart()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
	}

	bIsJumping = false;

	if (!IsInAirForAnimation() && !bIsLanding && !bLandingRequested)
	{
		bIsInAir = false;
		bWasInAir = false;
		bSuppressFallOffStart = false;
	}
}

void UBasePlayerAnimStateComponent::StartFallOffStart()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bIsInAir = true;
	bIsFallOffStart = true;

	World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
	World->GetTimerManager().SetTimer(
		FallOffStartTimerHandle,
		this,
		&UBasePlayerAnimStateComponent::FinishFallOffStart,
		FallOffStartDuration,
		false
	);
}

void UBasePlayerAnimStateComponent::FinishFallOffStart()
{
	StopFallOffStart();
}

void UBasePlayerAnimStateComponent::StopFallOffStart()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
	}

	bIsFallOffStart = false;
}

void UBasePlayerAnimStateComponent::HandleLanded(const FHitResult& Hit)
{
	StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(CachedBasePlayer ? CachedBasePlayer->GetVelocity().Z : 0.f)), true);
}

void UBasePlayerAnimStateComponent::StartLanding(float ImpactFallSpeed, bool bTriggerRealLandEvent)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
	StopFallOffStart();

	LandStartGroundSpeed = GroundSpeed;
	LandStartFallSpeed = ImpactFallSpeed;
	LastFallSpeed = ImpactFallSpeed;
	bLandWasMoving = LandStartGroundSpeed > IdleSpeedThreshold || bHasMoveInput;
	bLandWasSprinting = bIsSprinting || LandStartGroundSpeed >= RunToSprintSpeedThreshold;
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;

	bIsJumping = false;
	bIsInAir = true;
	bIsPhysicallyInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bIsLanding = true;
	bLandingRequested = true;
	bCanEnterLand = true;
	bCanEnterGround = false;

	World->GetTimerManager().ClearTimer(LandingTimerHandle);
	World->GetTimerManager().SetTimer(
		LandingTimerHandle,
		this,
		&UBasePlayerAnimStateComponent::FinishLanding,
		LandingDuration,
		false
	);

	World->GetTimerManager().ClearTimer(LandingRequestTimerHandle);
	World->GetTimerManager().SetTimer(
		LandingRequestTimerHandle,
		this,
		&UBasePlayerAnimStateComponent::FinishLandingRequest,
		LandingRequestDuration,
		false
	);

	if (bTriggerRealLandEvent && CachedBasePlayer && LandStartFallSpeed >= RealLandingEventSpeedThreshold)
	{
		CachedBasePlayer->K2_OnRealLanded();
	}
}

void UBasePlayerAnimStateComponent::FinishLanding()
{
	bIsLanding = false;
	bCanEnterGround = !bIsInAir && !bLandingRequested;
}

void UBasePlayerAnimStateComponent::FinishLandingRequest()
{
	bIsLanding = false;
	bLandingRequested = false;
	bIsInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = true;
}
