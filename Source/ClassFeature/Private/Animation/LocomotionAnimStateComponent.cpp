#include "Animation/LocomotionAnimStateComponent.h"
#include "BasePlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ULocomotionAnimStateComponent::ULocomotionAnimStateComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics; // Tick before physics to update inputs

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

    AirborneDuration = 0.f;
    bWasAirborneLastFrame = false;
    PreviousMoveInput = FVector2D::ZeroVector;
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
}

bool ULocomotionAnimStateComponent::PerformGroundProbe() const
{
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

    bPrevHasMoveInput = bHasMoveInput;

    // 1. Gather raw data from Character
    Velocity = CachedBasePlayer->GetVelocity();
    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.f;
    GroundSpeed = HorizontalVelocity.Size();
    VerticalSpeed = Velocity.Z;
    Acceleration = MovementComponent->GetCurrentAcceleration();
    bIsCombatMode = CachedBasePlayer->bIsCombatMode;

    // Setup input variables

    // Detect airborne state
    bool bPhysicallyFalling = MovementComponent->IsFalling();
    bIsInAir = bPhysicallyFalling;

    // ground probe for simulated proxies to prevent jitter
    if (bIsInAir && CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy)
    {
        if (FMath::Abs(VerticalSpeed) < 150.f)
        {
            if (PerformGroundProbe())
            {
                bIsInAir = false; // Override falling flag
            }
        }
    }

    // Sync compatibility variables
    bIsPhysicallyInAir = bIsInAir;
    bCanEnterGround = !bIsInAir;

    // 2. Manage airborne durations and falling variables
    if (bIsInAir)
    {
        AirborneDuration += DeltaTime;
        if (!bWasAirborneLastFrame)
        {
            bWasAirborneLastFrame = true;
        }
    }
    else
    {
        if (bWasAirborneLastFrame)
        {
            // Transition from Air to Ground
            LastFallSpeed = FMath::Abs(VerticalSpeed);
            bWasAirborneLastFrame = false;
        }
    }

    // Detect Sharp Turn
    bSharpTurnRequested = false;
    if (bHasMoveInput && PreviousMoveInput.SizeSquared() > 0.01f)
    {
        float Angle = FMath::Abs(FMath::FindDeltaAngleDegrees(
            FMath::Atan2(MoveInput.Y, MoveInput.X),
            FMath::Atan2(PreviousMoveInput.Y, PreviousMoveInput.X)));
        if (Angle > SharpTurnAngleThreshold && GroundSpeed > SharpTurnMinSpeed)
        {
            bSharpTurnRequested = true;
        }
    }
    PreviousMoveInput = MoveInput;

    // 3. Update Locomotion State transitions
    UpdateStateTransitions(DeltaTime);
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
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
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
        GetWorld()->GetTimerManager().SetTimer(StartFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStartFallbackTimeout, 0.8f, false);
    }
    else if (CurrentState == ELocomotionState::Stop)
    {
        GetWorld()->GetTimerManager().SetTimer(StopFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStopFallbackTimeout, 0.8f, false);
    }
    else if (CurrentState == ELocomotionState::InAir)
    {
        if (!bIsJumping)
        {
            bIsFallOffStart = true;
            GetWorld()->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishFallOffStart, 0.6f, false);
        }
    }
    else if (CurrentState == ELocomotionState::Landing)
    {
        bUseHeavyLand = (LastFallSpeed > HeavyLandSpeedThreshold);
        GetWorld()->GetTimerManager().SetTimer(LandingFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnLandingFallbackTimeout, 0.5f, false);
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
        if (bHasMoveInput)
        {
            ForceStateTransition(ELocomotionState::Locomotion);
        }
        else
        {
            ForceStateTransition(ELocomotionState::Idle);
        }
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
}

void ULocomotionAnimStateComponent::ClearMoveInput()
{
    MoveInput = FVector2D::ZeroVector;
    MoveInputSize = 0.f;
    bHasMoveInput = false;
}

void ULocomotionAnimStateComponent::HandleJumpStarted()
{
    bIsJumping = true;
    bIsInAir = true;
    AirborneDuration = 0.f;
    ForceStateTransition(ELocomotionState::InAir);

    if (CachedBasePlayer && GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(JumpStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishJumpStart, CachedBasePlayer->JumpStartMaxDuration, false);
    }
}

void ULocomotionAnimStateComponent::HandleLanded(const FHitResult& Hit, float ImpactFallSpeed)
{
    bIsJumping = false;
    bIsInAir = false;
    LastFallSpeed = ImpactFallSpeed;
    bWasAirborneLastFrame = false;

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    }
}

void ULocomotionAnimStateComponent::FinishJumpStart()
{
    bIsJumping = false;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    }
}

void ULocomotionAnimStateComponent::FinishFallOffStart()
{
    bIsFallOffStart = false;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
    }
}
