#include "Animation/LocomotionAnimStateComponent.h"
#include "BasePlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ULocomotionAnimStateComponent::ULocomotionAnimStateComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    CurrentState = ELocomotionState::Idle;
    PreviousState = ELocomotionState::Idle;
    GroundSpeed = 0.f;
    VerticalSpeed = 0.f;
    Acceleration = FVector::ZeroVector;
    Velocity = FVector::ZeroVector;
    bHasMoveInput = false;
    MoveInputSize = 0.f;
    MoveInput = FVector2D::ZeroVector;
    bSharpTurnRequested = false;
    bStartRequested = false;
    bStopRequested = false;
    bIsInAir = false;
    bIsJumping = false;
    bIsLanding = false;
    bUseHeavyLand = false;
    LastFallSpeed = 0.f;
    bIsCombatMode = false;

    // Backward-compatibility properties
    bIsPhysicallyInAir = false;
    bIsFallOffStart = false;
    bLandingRequested = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    bPrevHasMoveInput = false;
    CachedMoveInput = FVector2D::ZeroVector;
    bIsSprinting = false;
    MoveInputHeldTime = 0.f;
    CurrentStartToLoopDelay = 0.f;
    bUseStartDatabase = false;
    bGroundStartFinished = false;
    bPendingGroundStartFinish = false;
    bStartWasSprinting = false;
    bUseLoopDatabase = false;
    bUseSharpTurnDatabase = false;
    MoveInputTurnAngle = 0.f;
    PreviousMoveInputForTurn = FVector2D::ZeroVector;
    LandStartGroundSpeed = 0.f;
    LandStartFallSpeed = 0.f;
    bLandWasMoving = false;
    bLandWasSprinting = false;
    MovementDirection = 0.f;
    CombatInputForward = 0.f;
    CombatInputRight = 0.f;
    CombatForwardSpeed = 0.f;
    CombatRightSpeed = 0.f;

    // Default tuning values
    IdleSpeedThreshold = 15.f;
    SharpTurnAngleThreshold = 60.f;
    SharpTurnMinSpeed = 450.f;
    HeavyLandSpeedThreshold = 600.f;
    MoveInputDeadZone = 0.1f;
    StopIntentSpeedThreshold = 80.f;
    RunToSprintSpeedThreshold = 500.f;
    MoveInputTurnDeadZoneAngle = 5.f;
    StartMaxDuration = 0.8f;
    StopMaxDuration = 0.8f;
    JumpStartMaxDuration = 1.0f;
    FallOffStartMaxDuration = 0.8f;
    LandingMaxDuration = 0.8f;
    RealLandingEventSpeedThreshold = 300.f;
    WalkSpeed = 500.f;
    SprintSpeed = 700.f;
    WalkRotationRateYaw = 500.f;
    SprintRotationRateYaw = 500.f;
    GenericMoveInputSpeedThreshold = 3.f;

    AirborneDuration = 0.f;
    bWasAirborneLastFrame = false;
    PreviousMoveInput = FVector2D::ZeroVector;
    bWasInAir = false;
    bSuppressFallOffStart = false;
}

void ULocomotionAnimStateComponent::BeginPlay()
{
    Super::BeginPlay();
    CacheOwner();
}

void ULocomotionAnimStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!CachedBasePlayer)
    {
        CacheOwner();
        if (!CachedBasePlayer) return;
    }

    UpdateAnimationState(DeltaTime);
}

void ULocomotionAnimStateComponent::CacheOwner()
{
    CachedBasePlayer = Cast<ABasePlayer>(GetOwner());
    if (!CachedBasePlayer)
    {
        return;
    }

    bIsSprinting = CachedBasePlayer->bIsSprinting;
}

bool ULocomotionAnimStateComponent::IsDedicatedServer() const
{
    return GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer;
}

bool ULocomotionAnimStateComponent::PerformGroundProbe() const
{
    if (IsDedicatedServer()) return false;
    if (!CachedBasePlayer) return false;

    UCapsuleComponent* Capsule = CachedBasePlayer->GetCapsuleComponent();
    if (!Capsule) return false;

    float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    // Trace from slightly above capsule bottom to 40 units below
    FVector StartPos = CachedBasePlayer->GetActorLocation() + FVector(0.f, 0.f, -HalfHeight + 10.f);
    FVector EndPos = StartPos - FVector(0.f, 0.f, 40.f);

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(CachedBasePlayer);

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartPos, EndPos, ECC_Visibility, Params);
    return bHit;
}

void ULocomotionAnimStateComponent::UpdateAnimationState(float DeltaTime)
{
    if (!CachedBasePlayer) return;

    UCharacterMovementComponent* MovementComponent = CachedBasePlayer->GetCharacterMovement();
    if (!MovementComponent) return;

    Velocity = CachedBasePlayer->GetVelocity();
    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.f;
    GroundSpeed = HorizontalVelocity.Size();
    VerticalSpeed = Velocity.Z;
    Acceleration = MovementComponent->GetCurrentAcceleration();
    bIsCombatMode = CachedBasePlayer->bIsCombatMode;

    if (VerticalSpeed < 0.f)
    {
        LastFallSpeed = FMath::Abs(VerticalSpeed);
    }

    UpdateAirState(DeltaTime);
    UpdateMovementRequestState(DeltaTime);
    UpdateStateTransitions(DeltaTime);
    UpdateMaxWalkSpeed();
    UpdateCombatMovementState();
}

bool ULocomotionAnimStateComponent::IsInAirForAnimation() const
{
    const UCharacterMovementComponent* MovementComponent = CachedBasePlayer ? CachedBasePlayer->GetCharacterMovement() : nullptr;
    return MovementComponent && MovementComponent->MovementMode == MOVE_Falling;
}

bool ULocomotionAnimStateComponent::ShouldUseLocalInput() const
{
    return CachedBasePlayer && CachedBasePlayer->IsLocallyControlled();
}

FVector2D ULocomotionAnimStateComponent::GetMovementInputForState() const
{
    if (IsDedicatedServer())
    {
        return FVector2D::ZeroVector;
    }

    if (!CachedBasePlayer)
    {
        return FVector2D::ZeroVector;
    }

    if (ShouldUseLocalInput())
    {
        return MoveInput.GetClampedToMaxSize(1.f);
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

void ULocomotionAnimStateComponent::UpdateAirState(float DeltaTime)
{
    const bool bNowPhysicallyInAir = IsInAirForAnimation();
    bool bNowInAirForAnimation = bNowPhysicallyInAir;

    if (bNowInAirForAnimation && CachedBasePlayer && CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy && FMath::Abs(VerticalSpeed) < 150.f)
    {
        bNowInAirForAnimation = !PerformGroundProbe();
    }

    bIsPhysicallyInAir = bNowPhysicallyInAir;

    if (bNowInAirForAnimation)
    {
        AirborneDuration += DeltaTime;
        bWasAirborneLastFrame = true;
    }
    else if (bWasAirborneLastFrame)
    {
        bWasAirborneLastFrame = false;
    }

    if (bWasInAir && !bNowInAirForAnimation && !bIsLanding && !bLandingRequested)
    {
        const float ImpactFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
        if (AirborneDuration < 0.08f || ImpactFallSpeed < 100.f)
        {
            bIsInAir = false;
            bWasInAir = false;
            bSuppressFallOffStart = false;
            LastFallSpeed = 0.f;
        }
        else
        {
            StartLanding(ImpactFallSpeed, false);
        }
    }
    else if (!bWasInAir
        && bNowInAirForAnimation
        && !bIsJumping
        && !bIsLanding
        && !bSuppressFallOffStart)
    {
        StartFallOffStart();
    }

    if (bNowInAirForAnimation)
    {
        bIsInAir = true;
    }
    else if (!bIsJumping && !bLandingRequested && !bIsLanding)
    {
        bIsInAir = false;
        bSuppressFallOffStart = false;
    }

    bWasInAir = bNowInAirForAnimation;
    bCanEnterLand = bLandingRequested;
    bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;
}

void ULocomotionAnimStateComponent::UpdateMovementRequestState(float DeltaTime)
{
    bPrevHasMoveInput = bHasMoveInput;

    const FVector2D MovementInput = GetMovementInputForState();
    CachedMoveInput = MovementInput;

    MoveInputSize = MovementInput.Size();
    bHasMoveInput = MoveInputSize > MoveInputDeadZone || (!ShouldUseLocalInput() && GroundSpeed > GenericMoveInputSpeedThreshold);
    MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.f;

    if (IsDedicatedServer())
    {
        MoveInputTurnAngle = 0.f;
        bSharpTurnRequested = false;
        bStartRequested = false;
        bStopRequested = false;
        bUseStartDatabase = false;
        bUseLoopDatabase = bHasMoveInput;
        bUseSharpTurnDatabase = false;
        PreviousMoveInputForTurn = FVector2D::ZeroVector;
        return;
    }

    MoveInputTurnAngle = 0.f;
    bSharpTurnRequested = false;
    bStartRequested = false;
    bStopRequested = false;
    bUseStartDatabase = false;
    bUseLoopDatabase = false;
    bUseSharpTurnDatabase = false;

    if (bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone && MovementInput.Size() > MoveInputDeadZone)
    {
        const FVector2D PrevDir = PreviousMoveInputForTurn.GetSafeNormal();
        const FVector2D CurrDir = MovementInput.GetSafeNormal();
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
        PreviousMoveInputForTurn = bHasMoveInput ? MovementInput : FVector2D::ZeroVector;
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

    bSharpTurnRequested =
        bIsSprinting &&
        bHasMoveInput &&
        bPrevHasMoveInput &&
        GroundSpeed >= SharpTurnMinSpeed &&
        FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;

    bStartRequested = ShouldUseLocalInput() && bJustStartedMoving;
    bStopRequested = bJustStoppedMoving && GroundSpeed > StopIntentSpeedThreshold;
    CurrentStartToLoopDelay = 0.f;
    bUseStartDatabase = ShouldUseLocalInput() && bHasMoveInput && !bGroundStartFinished;
    bUseSharpTurnDatabase = ShouldUseLocalInput() && bSharpTurnRequested;
    bUseLoopDatabase = bHasMoveInput && !bUseStartDatabase && !bUseSharpTurnDatabase && !bStopRequested;

    PreviousMoveInputForTurn = bHasMoveInput ? MovementInput : FVector2D::ZeroVector;
}

void ULocomotionAnimStateComponent::UpdateCombatMovementState()
{
    if (IsDedicatedServer())
    {
        CombatInputRight = 0.f;
        CombatInputForward = 0.f;
        MovementDirection = 0.f;
        CombatForwardSpeed = 0.f;
        CombatRightSpeed = 0.f;
        return;
    }

    const FVector2D CombatMoveInput = GetMovementInputForState();
    CombatInputRight = CombatMoveInput.X;
    CombatInputForward = CombatMoveInput.Y;

    if (CachedBasePlayer && CachedBasePlayer->bIsCombatMode && bHasMoveInput)
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

void ULocomotionAnimStateComponent::UpdateMaxWalkSpeed() const
{
    if (!CachedBasePlayer || (!CachedBasePlayer->IsLocallyControlled() && !CachedBasePlayer->HasAuthority()))
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

void ULocomotionAnimStateComponent::ClearMovementRequests()
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

void ULocomotionAnimStateComponent::UpdateStateTransitions(float DeltaTime)
{
    PreviousState = CurrentState;

    // If combat mode is active and we are not in air or landing, default to combat locomotion/idle
    if (bIsCombatMode && CurrentState != ELocomotionState::InAir && CurrentState != ELocomotionState::Landing)
    {
        CurrentState = ELocomotionState::Combat;
        return;
    }

    switch (CurrentState)
    {
        case ELocomotionState::Idle:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (bHasMoveInput)
            {
                ForceStateTransition(ELocomotionState::Start);
            }
            break;
        }
        case ELocomotionState::Start:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (!bHasMoveInput)
            {
                ForceStateTransition(ELocomotionState::Stop);
            }
            break;
        }
        case ELocomotionState::Locomotion:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (!bHasMoveInput)
            {
                ForceStateTransition(ELocomotionState::Stop);
            }
            break;
        }
        case ELocomotionState::Stop:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (bHasMoveInput)
            {
                ForceStateTransition(ELocomotionState::Start);
            }
            break;
        }
        case ELocomotionState::InAir:
        {
            if (!bIsInAir)
            {
                // Micro-fall filter: ignore landing for minor drops
                if (AirborneDuration < 0.08f || LastFallSpeed < 100.f)
                {
                    AirborneDuration = 0.f;
                    if (bHasMoveInput && GroundSpeed > IdleSpeedThreshold)
                    {
                        ForceStateTransition(ELocomotionState::Locomotion);
                    }
                    else
                    {
                        ForceStateTransition(ELocomotionState::Idle);
                    }
                }
                else
                {
                    // Simulated Proxy residual velocity override for standing land
                    if (CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy && !bHasMoveInput)
                    {
                        // Ignore residual velocity by setting ground speed checks to standing land
                        LastFallSpeed = 100.f; // minimal threshold just to trigger landing
                    }

                    ForceStateTransition(ELocomotionState::Landing);
                }
            }
            break;
        }
        case ELocomotionState::Landing:
        {
            if (bIsPhysicallyInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (!bLandWasMoving && bHasMoveInput)
            {
                InterruptLandingForMoveInput();
            }
            break;
        }
        case ELocomotionState::Combat:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (!bIsCombatMode)
            {
                if (bHasMoveInput)
                {
                    ForceStateTransition(ELocomotionState::Locomotion);
                }
                else
                {
                    ForceStateTransition(ELocomotionState::Idle);
                }
            }
            break;
        }
    }
}

void ULocomotionAnimStateComponent::ForceStateTransition(ELocomotionState NewState)
{
    if (CurrentState == NewState) return;

    // Clear active fallback timers when leaving transitional states
    if (CurrentState == ELocomotionState::Start)
    {
        GetWorld()->GetTimerManager().ClearTimer(StartFallbackTimerHandle);
    }
    else if (CurrentState == ELocomotionState::Stop)
    {
        GetWorld()->GetTimerManager().ClearTimer(StopFallbackTimerHandle);
    }
    else if (CurrentState == ELocomotionState::InAir)
    {
        GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(JumpStartTimerHandle);
        bIsFallOffStart = false;
        bIsJumping = false;
        if (NewState != ELocomotionState::Landing)
        {
            LastFallSpeed = 0.f;
        }
    }
    else if (CurrentState == ELocomotionState::Landing)
    {
        GetWorld()->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
        LastFallSpeed = 0.f;
    }

    PreviousState = CurrentState;
    CurrentState = NewState;

    // Initialize timers when entering transitional states
    if (CurrentState == ELocomotionState::Start)
    {
        GetWorld()->GetTimerManager().SetTimer(StartFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStartFallbackTimeout, FMath::Max(0.1f, StartMaxDuration), false);
    }
    else if (CurrentState == ELocomotionState::Stop)
    {
        GetWorld()->GetTimerManager().SetTimer(StopFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStopFallbackTimeout, FMath::Max(0.1f, StopMaxDuration), false);
    }
    else if (CurrentState == ELocomotionState::InAir)
    {
        if (!bIsJumping)
        {
            bIsFallOffStart = true;
            GetWorld()->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishFallOffStart, FMath::Max(0.1f, FallOffStartMaxDuration), false);
        }
    }
    else if (CurrentState == ELocomotionState::Landing)
    {
        GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
        bUseHeavyLand = (LastFallSpeed > HeavyLandSpeedThreshold);
        bIsFallOffStart = false;
        GetWorld()->GetTimerManager().SetTimer(LandingFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnLandingFallbackTimeout, FMath::Max(0.1f, LandingMaxDuration), false);
    }
}

void ULocomotionAnimStateComponent::NotifyStartFinished()
{
    if (CurrentState == ELocomotionState::Start)
    {
        GetWorld()->GetTimerManager().ClearTimer(StartFallbackTimerHandle);
        if (bHasMoveInput)
        {
            ForceStateTransition(ELocomotionState::Locomotion);
        }
        else
        {
            ForceStateTransition(ELocomotionState::Stop);
        }
    }
}

void ULocomotionAnimStateComponent::NotifyStopFinished()
{
    if (CurrentState == ELocomotionState::Stop)
    {
        GetWorld()->GetTimerManager().ClearTimer(StopFallbackTimerHandle);
        ForceStateTransition(ELocomotionState::Idle);
    }
}

void ULocomotionAnimStateComponent::NotifyLandingFinished()
{
    if (CurrentState == ELocomotionState::Landing)
    {
        GetWorld()->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
        FinishLandingRequest();
    }
}

void ULocomotionAnimStateComponent::OnStartFallbackTimeout()
{
    NotifyStartFinished();
}

void ULocomotionAnimStateComponent::OnStopFallbackTimeout()
{
    NotifyStopFinished();
}

void ULocomotionAnimStateComponent::OnLandingFallbackTimeout()
{
    NotifyLandingFinished();
}

void ULocomotionAnimStateComponent::SetMoveInput(float Right, float Forward)
{
    MoveInput = FVector2D(Right, Forward);
    MoveInputSize = MoveInput.Size();
    bHasMoveInput = MoveInputSize > MoveInputDeadZone;
    CachedMoveInput = MoveInput.GetClampedToMaxSize(1.f);
}

void ULocomotionAnimStateComponent::ClearMoveInput()
{
    MoveInput = FVector2D::ZeroVector;
    MoveInputSize = 0.f;
    bHasMoveInput = false;
    CachedMoveInput = FVector2D::ZeroVector;
}

void ULocomotionAnimStateComponent::SetSprinting(bool bNewSprinting)
{
    bIsSprinting = bNewSprinting;
    UpdateMaxWalkSpeed();
}

void ULocomotionAnimStateComponent::HandleJumpStarted()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    World->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
    StopFallOffStart();

    bIsJumping = true;
    bIsInAir = true;
    bIsLanding = false;
    bLandingRequested = false;
    bCanEnterLand = false;
    bCanEnterGround = false;
    bWasInAir = true;
    bSuppressFallOffStart = true;
    AirborneDuration = 0.f;
    ForceStateTransition(ELocomotionState::InAir);

    if (CachedBasePlayer)
    {
        World->GetTimerManager().SetTimer(JumpStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishJumpStart, FMath::Max(0.1f, JumpStartMaxDuration), false);
    }
}

void ULocomotionAnimStateComponent::HandleRemoteJumpStarted(int32 EventSequence)
{
    if (!ShouldAcceptRemoteAnimEvent(EventSequence))
    {
        return;
    }

    HandleJumpStarted();
}

void ULocomotionAnimStateComponent::ApplyAuthoritativeSnapshot(const FReplicatedLocomotionState& Snapshot)
{
    if (CachedBasePlayer && CachedBasePlayer->HasAuthority())
    {
        return;
    }

    if (Snapshot.EventSequence < LastRemoteAnimEventSequence)
    {
        return;
    }

    LastRemoteAnimEventSequence = FMath::Max(LastRemoteAnimEventSequence, Snapshot.EventSequence);

    const uint8 MaxStateValue = static_cast<uint8>(ELocomotionState::Combat);
    const ELocomotionState AuthoritativeState = Snapshot.CurrentState <= MaxStateValue
        ? static_cast<ELocomotionState>(Snapshot.CurrentState)
        : ELocomotionState::Idle;

    if (CurrentState != AuthoritativeState)
    {
        PreviousState = CurrentState;
        CurrentState = AuthoritativeState;
    }

    bIsSprinting = Snapshot.bIsSprinting;
    bIsJumping = Snapshot.bIsJumping;
    bIsFallOffStart = Snapshot.bIsFallOffStart;
    bIsLanding = Snapshot.bIsLanding;
    bLandingRequested = Snapshot.bLandingRequested;
    bUseHeavyLand = Snapshot.bUseHeavyLand;
    bLandWasMoving = Snapshot.bLandWasMoving;
    bLandWasSprinting = Snapshot.bLandWasSprinting;
    LastFallSpeed = Snapshot.LastFallSpeed;

    bIsInAir = bIsJumping || bIsFallOffStart || bIsLanding || CurrentState == ELocomotionState::InAir;
    bCanEnterLand = bLandingRequested;
    bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;

    if (!bIsLanding && GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
    }
    if (!bIsJumping && GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    }
    if (!bIsFallOffStart && GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
    }

    UpdateMaxWalkSpeed();
}

void ULocomotionAnimStateComponent::HandleLanded(const FHitResult& Hit, float ImpactFallSpeed)
{
    StartLanding(ImpactFallSpeed, true);
}

void ULocomotionAnimStateComponent::FinishJumpStart()
{
    bIsJumping = false;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    }

    if (!IsInAirForAnimation() && !bIsLanding && !bLandingRequested)
    {
        bIsInAir = false;
        bWasInAir = false;
        bSuppressFallOffStart = false;
    }
}

void ULocomotionAnimStateComponent::FinishFallOffStart()
{
    StopFallOffStart();
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
    }
}

void ULocomotionAnimStateComponent::StartFallOffStart()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    bIsInAir = true;
    bIsFallOffStart = true;

    if (CachedBasePlayer && CachedBasePlayer->HasAuthority())
    {
        CachedBasePlayer->BroadcastFallOffStartedForRemoteClients();
    }

    World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
    World->GetTimerManager().SetTimer(
        FallOffStartTimerHandle,
        this,
        &ULocomotionAnimStateComponent::FinishFallOffStart,
        FMath::Max(0.1f, FallOffStartMaxDuration),
        false
    );
}

void ULocomotionAnimStateComponent::StopFallOffStart()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
    }

    bIsFallOffStart = false;
}

void ULocomotionAnimStateComponent::StartLanding(float ImpactFallSpeed, bool bTriggerRealLandEvent)
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
    bWasAirborneLastFrame = false;
    AirborneDuration = 0.f;
    bSuppressFallOffStart = false;
    bIsLanding = true;
    bLandingRequested = true;
    bCanEnterLand = true;
    bCanEnterGround = false;

    ForceStateTransition(ELocomotionState::Landing);

    if (bTriggerRealLandEvent && CachedBasePlayer && LandStartFallSpeed >= RealLandingEventSpeedThreshold)
    {
        CachedBasePlayer->K2_OnRealLanded();
    }
}

void ULocomotionAnimStateComponent::HandleRemoteFallOffStarted(int32 EventSequence)
{
    if (!ShouldAcceptRemoteAnimEvent(EventSequence))
    {
        return;
    }

    if (CurrentState == ELocomotionState::InAir && bIsFallOffStart)
    {
        return;
    }

    bIsJumping = false;
    bIsLanding = false;
    bLandingRequested = false;
    bCanEnterLand = false;
    bCanEnterGround = false;
    bWasInAir = true;
    bSuppressFallOffStart = false;
    AirborneDuration = 0.f;

    ForceStateTransition(ELocomotionState::InAir);
}

void ULocomotionAnimStateComponent::HandleRemoteLanded(float ImpactFallSpeed, int32 EventSequence)
{
    if (!ShouldAcceptRemoteAnimEvent(EventSequence))
    {
        return;
    }

    StartLanding(ImpactFallSpeed, false);
}

bool ULocomotionAnimStateComponent::ShouldAcceptRemoteAnimEvent(int32 EventSequence)
{
    if (EventSequence <= LastRemoteAnimEventSequence)
    {
        return false;
    }

    LastRemoteAnimEventSequence = EventSequence;
    return true;
}

void ULocomotionAnimStateComponent::FinishLanding()
{
    bIsLanding = false;
    bCanEnterGround = !bIsInAir && !bLandingRequested;
}

void ULocomotionAnimStateComponent::FinishLandingRequest()
{
    bIsLanding = false;
    bLandingRequested = false;
    bIsInAir = false;
    bWasInAir = false;
    bWasAirborneLastFrame = false;
    AirborneDuration = 0.f;
    bSuppressFallOffStart = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;

    if (bHasMoveInput)
    {
        ForceStateTransition(ELocomotionState::Locomotion);
    }
    else
    {
        ForceStateTransition(ELocomotionState::Idle);
    }
}

void ULocomotionAnimStateComponent::InterruptLandingForMoveInput()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
    }

    bIsLanding = false;
    bLandingRequested = false;
    bIsInAir = false;
    bWasInAir = false;
    bWasAirborneLastFrame = false;
    AirborneDuration = 0.f;
    bSuppressFallOffStart = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;

    MoveInputHeldTime = 0.f;
    bGroundStartFinished = !ShouldUseLocalInput();
    bPendingGroundStartFinish = false;
    bStartWasSprinting = bIsSprinting;
    bStartRequested = ShouldUseLocalInput();
    bStopRequested = false;
    bUseStartDatabase = ShouldUseLocalInput();
    bUseLoopDatabase = false;
    bUseSharpTurnDatabase = false;

    if (bIsCombatMode)
    {
        ForceStateTransition(ELocomotionState::Combat);
    }
    else
    {
        ForceStateTransition(ELocomotionState::Start);
    }
}
