#include "Animation/LocomotionAnimStateComponent.h"
#include "BasePlayer.h"
#include "SwimmingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#ifdef UE_LOG
#undef UE_LOG
#endif
#define UE_LOG(...)

namespace
{
    bool IsStopDebugEnabled()
    {
        const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("a.StopDebug"));
        return DebugCVar && DebugCVar->GetInt() > 0;
    }

    void EmitStopDebug(const FString& Line)
    {
        if (IsStopDebugEnabled())
        {
            // This translation unit intentionally undefines UE_LOG to prevent
            // legacy capture spam. Use FMsg directly for the opt-in Stop probe.
            FMsg::Logf(__FILE__, __LINE__, FName(TEXT("LogTemp")), ELogVerbosity::Display, TEXT("%s"), *Line);
        }
    }

    bool IsMotionMatchingCaptureEnabled()
    {
        const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MMDebugging"));
        return DebugCVar && DebugCVar->GetInt() > 0;
    }

    void AppendMotionMatchingCaptureLine(const FString& Line)
    {
        // 파일 입출력을 제거하여 퍼포먼스 드랍 방지. 기존 UE_LOG(LogTemp)로 대체됨.
    }
}

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
    bGroundMoveEpisodeActive = false;
    bIsInAir = false;
    bIsJumping = false;
    bJumpStartWasMoving = false;
    JumpStartGroundSpeed = 0.f;
    JumpStartMoveDirection = FVector2D::ZeroVector;
    bIsLanding = false;
    bUseHeavyLand = false;
    LastFallSpeed = 0.f;
    bIsCombatMode = false;
    bIsTurningInPlace = false;
    bTurnInPlacePhaseActive = false;
    TurnInPlacePhaseElapsed = 0.0f;
    bIsLocomotionTransitioning = false;
    LocomotionTransitionTimer = 0.f;

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
    LandMoveDirection = FVector2D::ZeroVector;
    bLandDirectionFromVelocity = false;
    LandingStartMoveInput = FVector2D::ZeroVector;
    LandingElapsedTime = 0.f;
    LandingStartControlYaw = 0.f;
    bLandingFromFallOff = false;
    MovementDirection = 0.f;
    CombatInputForward = 0.f;
    CombatInputRight = 0.f;
    CombatForwardSpeed = 0.f;
    CombatRightSpeed = 0.f;

    // Default tuning values
    IdleSpeedThreshold = 15.f;
    SharpTurnAngleThreshold = 60.f;
    SharpTurnMinSpeed = 450.f;
    PivotAngleThreshold = 110.f;
    PivotMinSpeed = 350.f;
    HeavyLandSpeedThreshold = 600.f;
    MoveInputDeadZone = 0.1f;
    StopIntentSpeedThreshold = 80.f;
    RunToSprintSpeedThreshold = 500.f;
    MoveInputTurnDeadZoneAngle = 5.f;
    StartMaxDuration = 0.8f;
    StopMaxDuration = 0.8f;
    JumpStartMaxDuration = 1.0f;
    FallOffStartMaxDuration = 2.0f;
    LandingMaxDuration = 0.8f;
    RealLandingEventSpeedThreshold = 300.f;
    MinimumLandingDuration = 0.45f;
    RemoteMinimumLandingDuration = 0.65f;
    FallOffMinimumLandingDuration = 0.60f;
    LandingDirectionInterruptMinTime = 0.18f;
    LandingInputDirectionInterruptAngle = 45.f;
    LandingControlYawInterruptAngle = 55.f;
    SprintDiagonalLandingDuration = 0.16f;
    SprintDiagonalLandingRightThreshold = 0.25f;
    DiagonalLandingForwardThreshold = 0.25f;
    WalkSpeed = 500.f;
    SprintSpeed = 700.f;
    WalkRotationRateYaw = 500.f;
    SprintRotationRateYaw = 500.f;
    StartAlignControlYawThreshold = 90.f;
    GenericMoveInputSpeedThreshold = 3.f;

    AirborneDuration = 0.f;
    bWasAirborneLastFrame = false;
    PreviousMoveInput = FVector2D::ZeroVector;
    bWasInAir = false;
    bSuppressFallOffStart = false;
    GroundedConfirmTimer = 0.f;
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

    // bIsSprinting = CachedBasePlayer->bIsSprinting;

    if (USkeletalMeshComponent* Mesh = CachedBasePlayer->GetMesh())
    {
        DefaultMeshRelativeRotation = Mesh->GetRelativeRotation();
    }
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

    if (CurrentState == ELocomotionState::Landing)
    {
        LandingElapsedTime += DeltaTime;
    }
    else
    {
        LandingElapsedTime = 0.f;
    }

    if (VerticalSpeed < 0.f)
    {
        LastFallSpeed = FMath::Abs(VerticalSpeed);
    }

    UpdateAirState(DeltaTime);
    UpdateMovementRequestState(DeltaTime);
    UpdateStateTransitions(DeltaTime);
    UpdateTurnInPlacePhase(DeltaTime);

    UpdateMaxWalkSpeed();
    UpdateCombatMovementState();

    // ?쒖옄由??뚯쟾(Turn In Place) ?곹깭 癒몄떊 湲곕컲 Desired Rotation ?쒖뼱
    // TIP is a derived presentation phase, never the legacy transient
    // ELocomotionState::TurnInPlace. Do not overwrite the phase result.
    bIsTurningInPlace = bTurnInPlacePhaseActive;

    // ?낅젰 諛⑺뼢怨??ㅼ젣 ?띾룄 諛⑺뼢???ㅼ감瑜?怨꾩궛?섏뿬 諛⑺뼢 ?꾪솚 ?곹깭(Transition) ?먮퀎
    bool bPhysicallyTransitioning = false;
    if (bHasMoveInput && GroundSpeed > 50.f)
    {
        // 罹먮┃??以묒떖 湲곗????대룞 ?낅젰 Yaw
        const float InputYaw = FMath::RadiansToDegrees(FMath::Atan2(CombatInputRight, CombatInputForward));

        // ?붾뱶 ?띾룄 踰≫꽣瑜?罹먮┃??濡쒖뺄 怨듦컙?쇰줈 蹂?섑븯??濡쒖뺄 ?띾룄 Yaw ?곗궛
        const FVector LocalVelocity = CachedBasePlayer->GetActorTransform().InverseTransformVector(HorizontalVelocity);
        const float VelocityYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));

        const float AngleDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(InputYaw, VelocityYaw));
        const FVector HorizontalAcceleration = FVector(Acceleration.X, Acceleration.Y, 0.f);
        const float WorldTrajectoryTurn =
            !HorizontalVelocity.IsNearlyZero() && !HorizontalAcceleration.IsNearlyZero()
            ? FMath::Abs(FMath::FindDeltaAngleDegrees(
                HorizontalVelocity.Rotation().Yaw,
                HorizontalAcceleration.Rotation().Yaw))
            : 0.f;

        // In an always-Strafe controller, holding W while rotating the camera
        // rotates actor, input and velocity together. Their *local* angle is
        // then still zero, so the old test never exposed PSD_Run_Tnasition.
        // World velocity lags behind the newly accelerated control direction
        // for that short redirect; include it to expose Box/Diamond candidates.
        bPhysicallyTransitioning = (AngleDiff > 20.f) || (WorldTrajectoryTurn > 20.f);
    }

    if (bPhysicallyTransitioning)
    {
        // 諛⑺뼢 ?꾪솚??媛먯??섎㈃ 理쒖냼 ?좎? ?쒓컙 ??대㉧瑜??뗮똿?섍퀬 ?꾪솚 ?곹깭 ?좎?
        LocomotionTransitionTimer = MinTransitionDuration;
        bIsLocomotionTransitioning = true;
    }
    else if (LocomotionTransitionTimer > 0.f)
    {
        // ??대㉧媛 ?숈옉?섎뒗 ?숈븞? ?ㅼ감媛 醫곹?議뚯뼱??怨꾩냽 ?꾪솚(Transition DB) ?곹깭瑜?蹂댁옣?섏뿬 ?쇰쿁 紐⑥뀡 ?꾨즺 ?좊룄
        LocomotionTransitionTimer -= DeltaTime;
        bIsLocomotionTransitioning = true;
    }
    else
    {
        bIsLocomotionTransitioning = false;
    }

    UpdateCharacterRotation(DeltaTime);
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

    if (bHasMoveInput && CachedMoveInput.SizeSquared() > FMath::Square(MoveInputDeadZone))
    {
        return CachedMoveInput.GetClampedToMaxSize(1.f);
    }

    return FVector2D::ZeroVector;
}

void ULocomotionAnimStateComponent::UpdateAirState(float DeltaTime)
{
    const bool bNowPhysicallyInAir = IsInAirForAnimation();
    bool bNowInAirForAnimation = bNowPhysicallyInAir;

    // Filter brief ledge/step-down ground contact while falling.
    if (bNowPhysicallyInAir)
    {
        GroundedConfirmTimer = 0.0f;
    }
    else if (bWasInAir)
    {
        // 0.1珥??숈븞 ?곗냽?쇰줈 ?뺤떎?섍쾶 吏€?곸뿉 癒몃Ъ?ъ빞 吏꾩쭨 吏€???곹깭濡??몄젙
        GroundedConfirmTimer += DeltaTime;
        if (GroundedConfirmTimer < 0.1f)
        {
            bNowInAirForAnimation = true; // ?꾩쭅?€ 怨듭쨷 ?곹깭 ?좎?
        }
    }

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

    if (bWasInAir && !bNowInAirForAnimation && !bIsLanding && !bLandingRequested && !bIsJumping)
    {
        const float ImpactFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
        if (AirborneDuration < 0.08f || ImpactFallSpeed < 100.f)
        {
            if (IsMotionMatchingCaptureEnabled())
            {
                const FString DebugLine = FString::Printf(
                    TEXT("[MMCAP_AIR] IgnoreLanding AirTime=%.3f Impact=%.1f Vertical=%.1f Ground=%.1f Input=(R=%.2f,F=%.2f) PhysAir=%d AnimAir=%d"),
                    AirborneDuration,
                    ImpactFallSpeed,
                    VerticalSpeed,
                    GroundSpeed,
                    CachedMoveInput.X,
                    CachedMoveInput.Y,
                    bNowPhysicallyInAir ? 1 : 0,
                    bNowInAirForAnimation ? 1 : 0);
                UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
                AppendMotionMatchingCaptureLine(DebugLine);
            }

            bIsInAir = false;
            bWasInAir = false;
            bSuppressFallOffStart = false;
            LastFallSpeed = 0.f;
        }
        else
        {
            if (IsMotionMatchingCaptureEnabled())
            {
                const FString DebugLine = FString::Printf(
                    TEXT("[MMCAP_AIR] GroundedAfterAir -> StartLanding AirTime=%.3f Impact=%.1f LastFall=%.1f Vertical=%.1f Ground=%.1f WasJump=%d WasFallOff=%d Input=(R=%.2f,F=%.2f)"),
                    AirborneDuration,
                    ImpactFallSpeed,
                    LastFallSpeed,
                    VerticalSpeed,
                    GroundSpeed,
                    bIsJumping ? 1 : 0,
                    bIsFallOffStart ? 1 : 0,
                    CachedMoveInput.X,
                    CachedMoveInput.Y);
                UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
                AppendMotionMatchingCaptureLine(DebugLine);
            }

            StartLanding(ImpactFallSpeed, false);
        }
    }
    else if (!bWasInAir
        && bNowInAirForAnimation
        && !bIsJumping
        && !bIsLanding
        && !bSuppressFallOffStart)
    {
        if (IsMotionMatchingCaptureEnabled())
        {
            const FString DebugLine = FString::Printf(
                TEXT("[MMCAP_AIR] DetectFallOff -> StartFallOff PhysAir=%d AnimAir=%d Jump=%d Landing=%d Suppress=%d Vel=(%.1f,%.1f,%.1f) Ground=%.1f Input=(R=%.2f,F=%.2f) State=%s"),
                bNowPhysicallyInAir ? 1 : 0,
                bNowInAirForAnimation ? 1 : 0,
                bIsJumping ? 1 : 0,
                bIsLanding ? 1 : 0,
                bSuppressFallOffStart ? 1 : 0,
                Velocity.X,
                Velocity.Y,
                Velocity.Z,
                GroundSpeed,
                CachedMoveInput.X,
                CachedMoveInput.Y,
                *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)));
            UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
            AppendMotionMatchingCaptureLine(DebugLine);
        }

        StartFallOffStart();
    }
    else if (!bWasInAir && bNowInAirForAnimation && IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_AIR] SkipFallOffStart PhysAir=%d AnimAir=%d Jump=%d Landing=%d LandingRequested=%d Suppress=%d Vel=(%.1f,%.1f,%.1f) Ground=%.1f Input=(R=%.2f,F=%.2f) State=%s"),
            bNowPhysicallyInAir ? 1 : 0,
            bNowInAirForAnimation ? 1 : 0,
            bIsJumping ? 1 : 0,
            bIsLanding ? 1 : 0,
            bLandingRequested ? 1 : 0,
            bSuppressFallOffStart ? 1 : 0,
            Velocity.X,
            Velocity.Y,
            Velocity.Z,
            GroundSpeed,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)));
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

    // bIsInAir should not be set back to true if we are in the middle of a Landing sequence!
    if (bNowInAirForAnimation && !bIsLanding)
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
    
    if (ShouldUseLocalInput())
    {
        bHasMoveInput = MoveInputSize > MoveInputDeadZone;
    }
    // For Simulated Proxy, bHasMoveInput is already updated from the snapshot, do not overwrite it based on velocity.
    
    MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.f;

    if (IsDedicatedServer())
    {
        MoveInputTurnAngle = 0.f;
        bSharpTurnRequested = false;
        bStartRequested = false;
        bStopRequested = false;
        bGroundMoveEpisodeActive = false;
        bUseStartDatabase = false;
        bUseLoopDatabase = bHasMoveInput;
        bUseSharpTurnDatabase = false;
        PreviousMoveInputForTurn = FVector2D::ZeroVector;
        return;
    }

    MoveInputTurnAngle = 0.f;
    bSharpTurnRequested = false;
    bStartRequested = false;
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

    const bool bCanRequestGroundMove = !bIsInAir && !bIsJumping && !bIsFallOffStart;
    if (!bCanRequestGroundMove)
    {
        ClearMovementRequests();
        PreviousMoveInputForTurn = bHasMoveInput ? MovementInput : FVector2D::ZeroVector;
        return;
    }

    // Do not derive Stop exclusively from bPrevHasMoveInput.  The movement
    // input is often cleared before this AnimStateComponent ticks, making the
    // sampled "previous" value already false on the release frame.  A move
    // episode is armed by any grounded input and consumed exactly once by the
    // State Controller when its direct Stop Blend Stack one-shot is selected.
    if (bHasMoveInput)
    {
        bGroundMoveEpisodeActive = true;
        bStopRequested = false;
    }

    const bool bJustStartedMoving = !bPrevHasMoveInput && bHasMoveInput;

    if (bJustStartedMoving)
    {
        MoveInputHeldTime = 0.f;
        bGroundStartFinished = !ShouldUseLocalInput();
        bPendingGroundStartFinish = false;
        bStartWasSprinting = bIsSprinting;
        // A fresh movement episode invalidates an unconsumed release request.
        bStopRequested = false;
    }

    if (!bHasMoveInput)
    {
        bGroundStartFinished = false;
        bPendingGroundStartFinish = false;
        bStartWasSprinting = false;
    }

    // Project_J distinguishes a normal turn redirect from a direct Pivot.
    // Artistic keeps 45/90 degree steering inside PSD_Run_Transition; only a
    // fast 180-ish reversal receives the authored Pivot one-shot.
    bSharpTurnRequested =
        bHasMoveInput &&
        bPrevHasMoveInput &&
        GroundSpeed >= PivotMinSpeed &&
        FMath::Abs(MoveInputTurnAngle) >= PivotAngleThreshold;

    bStartRequested = ShouldUseLocalInput() && bJustStartedMoving;
    // Keep this request until State Controller has actually committed the
    // direct Stop chooser.  The episode latch, rather than the raw edge or
    // instantaneous speed, is the authority for whether a Stop is owed.
    const bool bNewStopRequest = !bHasMoveInput && bGroundMoveEpisodeActive && !bStopRequested;
    bStopRequested = bStopRequested || bNewStopRequest;
    if (bNewStopRequest)
    {
        const FString StopEvent = FString::Printf(
            TEXT("Stop requested InputRelease Ground=%.1f PreviousState=%s"),
            GroundSpeed,
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)));
        RecordStateControllerDebugEvent(StopEvent);
        EmitStopDebug(FString::Printf(
            TEXT("[SC_STOP_COMPONENT] Event=Requested Input=%d PrevInput=%d Ground=%.1f State=%s Episode=%d Pending=%d"),
            bHasMoveInput ? 1 : 0,
            bPrevHasMoveInput ? 1 : 0,
            GroundSpeed,
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)),
            bGroundMoveEpisodeActive ? 1 : 0,
            bStopRequested ? 1 : 0));
    }
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

void ULocomotionAnimStateComponent::UpdateTurnInPlacePhase(float DeltaTime)
{
    // This deliberately mirrors Project_J's derived locomotion phase rather
    // than using Artistic's legacy ELocomotionState::TurnInPlace transition.
    // The latter is a transient state and immediately returns to Idle; a TIP
    // must instead remain a presentation request over stationary Idle/Stop
    // while the State Controller restarts the selected clip when needed.
    const bool bLocallyOwnedRotation = CachedBasePlayer &&
        (CachedBasePlayer->IsLocallyControlled() || CachedBasePlayer->HasAuthority());
    const bool bHighPriorityAction = CachedBasePlayer &&
        (CachedBasePlayer->bIsAttacking || CachedBasePlayer->bIsDodging || CachedBasePlayer->bIsHitReacting);
    // Allow TIP when there is no move input (even if slowing down from Stop or recovering from Land)
    const bool bStationaryOrStopping = !bHasMoveInput && (GroundSpeed <= 150.0f || bStopRequested || bIsLanding);
    const bool bCanTurnInPlace = bLocallyOwnedRotation && bStationaryOrStopping && !bHighPriorityAction && !bIsInAir;
    const float FacingDeltaYaw = CachedBasePlayer ? CachedBasePlayer->GetDesiredFacingDeltaYaw() : 0.0f;

    constexpr float EntryAngleDegrees = 30.0f;
    constexpr float ContinuationAngleDegrees = 5.0f;
    constexpr float MaximumPhaseSeconds = 1.5f;

    DesiredFacingDeltaYaw = FacingDeltaYaw;
    const bool bRawEntry = bCanTurnInPlace && FMath::Abs(FacingDeltaYaw) >= EntryAngleDegrees;
    const bool bContinue = bTurnInPlacePhaseActive && bCanTurnInPlace &&
        FMath::Abs(FacingDeltaYaw) > ContinuationAngleDegrees &&
        TurnInPlacePhaseElapsed < MaximumPhaseSeconds;

    if (!bTurnInPlacePhaseActive && bRawEntry)
    {
        TurnInPlacePhaseElapsed = 0.0f;
    }
    else if (bTurnInPlacePhaseActive && (bRawEntry || bContinue))
    {
        TurnInPlacePhaseElapsed += FMath::Max(DeltaTime, 0.0f);
    }
    else
    {
        TurnInPlacePhaseElapsed = 0.0f;
    }

    bTurnInPlacePhaseActive = bRawEntry || bContinue;
    bShouldTurnInPlace = bTurnInPlacePhaseActive;
    bIsTurningInPlace = bTurnInPlacePhaseActive;
    TurnInPlaceRootYawDelta = 0.0f;

    // TIP owns a stationary release episode. Do not defer old Stop or Landing requests
    if (bTurnInPlacePhaseActive)
    {
        if (bStopRequested)
        {
            EmitStopDebug(TEXT("[SC_STOP_COMPONENT] Event=CancelledByTIP"));
        }
        bStopRequested = false;
        bGroundMoveEpisodeActive = false;
        bLandingRequested = false;
        bIsLanding = false;
        if (CurrentState == ELocomotionState::Stop || CurrentState == ELocomotionState::Landing)
        {
            ForceStateTransition(ELocomotionState::Idle);
        }
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(StopFallbackTimerHandle);
            World->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
        }
    }

}

void ULocomotionAnimStateComponent::UpdateCharacterRotation(float DeltaTime)
{
    UCharacterMovementComponent* MovementComponent = CachedBasePlayer ? CachedBasePlayer->GetCharacterMovement() : nullptr;
    if (!MovementComponent) return;

    // Always keep camera-oriented rotation settings (showing back)
    MovementComponent->bOrientRotationToMovement = false;

    // Project_J rotation ownership: controller yaw is applied by the
    // character only while moving; stationary TIP applies the selected root
    // track.  CMC ControllerDesiredRotation must stay disabled in both cases
    // so it cannot overwrite that direct root-yaw delta a frame later.
    MovementComponent->bUseControllerDesiredRotation = false;

    // Interpolate mesh relative rotation offset back to default
    if (FMath::Abs(MeshYawOffset) > 0.01f)
    {
        MeshYawOffset = FMath::FInterpTo(MeshYawOffset, 0.f, DeltaTime, MeshYawOffsetInterpSpeed);
        if (CachedBasePlayer)
        {
            if (USkeletalMeshComponent* Mesh = CachedBasePlayer->GetMesh())
            {
                FRotator MeshRot = DefaultMeshRelativeRotation;
                MeshRot.Yaw += MeshYawOffset;
                Mesh->SetRelativeRotation(MeshRot);
            }
        }
    }
    else if (MeshYawOffset != 0.f)
    {
        MeshYawOffset = 0.f;
        if (CachedBasePlayer)
        {
            if (USkeletalMeshComponent* Mesh = CachedBasePlayer->GetMesh())
            {
                Mesh->SetRelativeRotation(DefaultMeshRelativeRotation);
            }
        }
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

	const USwimmingComponent* SwimmingComponent = CachedBasePlayer->GetSwimmingComponent();
	const bool bIsInShallowWater = SwimmingComponent && SwimmingComponent->IsInShallowWater();
	const bool bCanSprint =
		bIsSprinting &&
		!bIsInShallowWater &&
		!CachedBasePlayer->bIsAttacking &&
        !CachedBasePlayer->bIsDodging &&
        !CachedBasePlayer->bIsHitReacting;

    float TargetRotationRate = bCanSprint ? SprintRotationRateYaw : WalkRotationRateYaw;

	MovementComponent->MaxWalkSpeed = bIsInShallowWater
		? SwimmingComponent->GetShallowWaterMaxWalkSpeed()
		: (bCanSprint ? SprintSpeed : WalkSpeed);
    MovementComponent->RotationRate = FRotator(
        0.f,
        TargetRotationRate,
        0.f
    );
}

void ULocomotionAnimStateComponent::ClearMovementRequests()
{
    bStartRequested = false;
    bStopRequested = false;
    bGroundMoveEpisodeActive = false;
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

void ULocomotionAnimStateComponent::AlignActorYawToControlYawForStartIfNeeded()
{
	// Kept only as a compatibility call-site for the legacy locomotion enum.
	// Artistic is permanently Strafe: moving capsule yaw belongs to the
	// controller, stationary yaw belongs to the selected TIP root track.  A
	// Start-time SetActorRotation would introduce a third owner and visually
	// shorten the following direct turn or Start asset.
}

void ULocomotionAnimStateComponent::UpdateStateTransitions(float DeltaTime)
{
    PreviousState = CurrentState;

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
                AlignActorYawToControlYawForStartIfNeeded();
                ForceStateTransition(ELocomotionState::Start);
            }
            break;
        }
        case ELocomotionState::TurnInPlace:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else if (bHasMoveInput)
            {
                AlignActorYawToControlYawForStartIfNeeded();
                ForceStateTransition(ELocomotionState::Start);
            }
            else
            {
                ForceStateTransition(ELocomotionState::Idle);
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
            else if (bStartWasSprinting != bIsSprinting)
            {
                InterruptStartForGaitChange();
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
                AlignActorYawToControlYawForStartIfNeeded();
                ForceStateTransition(ELocomotionState::Start);
            }
            else if (bTurnInPlacePhaseActive)
            {
                ForceStateTransition(ELocomotionState::Idle);
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
            const bool bHadPostTouchdownMoveInput = bLandingReceivedMoveInput;
            if (!bPrevHasMoveInput && bHasMoveInput)
            {
                RecordStateControllerDebugEvent(FString::Printf(
                    TEXT("Landing input observed Time=%.3f Input=(%.2f,%.2f)"),
                    LandingElapsedTime,
                    CachedMoveInput.X,
                    CachedMoveInput.Y));
            }
            if (bHasMoveInput)
            {
                LandingPostTouchdownMoveInputTime += DeltaTime;
                bLandingReceivedMoveInput =
                    LandingPostTouchdownMoveInputTime >= LandingExitStopInputHoldTime;
            }

            if (!bHadPostTouchdownMoveInput && bLandingReceivedMoveInput)
            {
                RecordStateControllerDebugEvent(FString::Printf(
                    TEXT("Landing post-touchdown input committed Time=%.3f Held=%.3f Input=(%.2f,%.2f)"),
                    LandingElapsedTime,
                    LandingPostTouchdownMoveInputTime,
                    CachedMoveInput.X,
                    CachedMoveInput.Y));
            }
            if (bIsPhysicallyInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            // Sprint Land is authored for the sprint gait captured at impact.
            // If Sprint state changes (pressed or released) while movement input continues,
            // retaining that root-motion clip is visually wrong. Hand straight to the
            // regular moving Motion Matching query; its Pose History redirect
            // makes the graph transition seamless instead of exposing a stale
            // hidden locomotion loop.
            else if ((bLandWasSprinting != bIsSprinting) && bHasMoveInput)
            {
                InterruptSprintLandingForSprintRelease();
            }
            // Immediately hand off to Stop when the player releases movement input during a moving land
            else if (bLandWasMoving && !bHasMoveInput)
            {
                RecordStateControllerDebugEvent(FString::Printf(
                    TEXT("Landing input released -> Instant Stop Time=%.3f"), LandingElapsedTime));
                InterruptLandingForStop();
            }
            // A deliberate redirect has priority over the authored minimum Land
            // duration.  This used to live after the diagonal-Land completion
            // branch, which meant that once the diagonal minimum elapsed the
            // branch returned straight to the loop without ever checking a
            // new WASD direction or camera turn.
            else if (LandingElapsedTime >= LandingDirectionInterruptMinTime)
            {
                bool bInputDirectionChanged = false;
                float InputDirectionDelta = 0.f;
                const FVector2D RedirectReferenceDirection = !LandMoveDirection.IsNearlyZero()
                    ? LandMoveDirection.GetSafeNormal()
                    : LandingStartMoveInput.GetSafeNormal();
                if (bHasMoveInput && !RedirectReferenceDirection.IsNearlyZero() && !CachedMoveInput.IsNearlyZero())
                {
                    const float Dot = FMath::Clamp(FVector2D::DotProduct(
                        RedirectReferenceDirection, CachedMoveInput.GetSafeNormal()), -1.f, 1.f);
                    InputDirectionDelta = FMath::RadiansToDegrees(FMath::Acos(Dot));
                    bInputDirectionChanged = InputDirectionDelta >= LandingInputDirectionInterruptAngle;
                }

                bool bControlYawChanged = false;
                float ControlYawDelta = 0.f;
                if (CachedBasePlayer)
                {
                    ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
                        LandingStartControlYaw,
                        CachedBasePlayer->GetControlRotation().Yaw));
                    bControlYawChanged = ControlYawDelta >= LandingControlYawInterruptAngle;
                }

                LastLandingInputDirectionDelta = InputDirectionDelta;
                LastLandingControlYawDelta = ControlYawDelta;
                bLastLandingInputDirectionChanged = bInputDirectionChanged;
                bLastLandingControlYawChanged = bControlYawChanged;

                if (bInputDirectionChanged || bControlYawChanged)
                {
                    RecordStateControllerDebugEvent(FString::Printf(
                        TEXT("Landing redirect -> MotionMatching Time=%.3f InputChanged=%d(%.1f/%.1f) YawChanged=%d(%.1f/%.1f)"),
                        LandingElapsedTime,
                        bInputDirectionChanged ? 1 : 0,
                        InputDirectionDelta,
                        LandingInputDirectionInterruptAngle,
                        bControlYawChanged ? 1 : 0,
                        ControlYawDelta,
                        LandingControlYawInterruptAngle));
                    InterruptLandingForDirectionChange();
                    break;
                }

                if (IsDiagonalLanding() && LandingElapsedTime >= GetEffectiveMinimumLandingDuration())
                {
                    // Holding the same movement direction through touchdown is
                    // not a redirect.  The authored Land remains in control
                    // until either the direction/yaw gates above change, or an
                    // input-release edge requests the direct Stop.
                    if (bLandWasMoving && bLandingReceivedMoveInput && !bHasMoveInput)
                    {
                        InterruptLandingForStop();
                    }
                }
                else if (LandingElapsedTime >= MinimumLandingDuration)
                {
                    if (!bLandWasMoving)
                    {
                        // A standing land has no impact direction to compare
                        // against; a newly pressed input is therefore a real
                        // redirect and should hand straight to MM.
                        if (bHasMoveInput && !bLandingInputHeldAtTouchdown)
                        {
                            InterruptLandingForDirectionChange();
                        }
                    }
                    else // bLandWasMoving
                    {
                        if (bLandingReceivedMoveInput && !bHasMoveInput)
                        {
                            InterruptLandingForStop();
                        }
                    }
                }
            }
            break;
        }
        case ELocomotionState::Combat:
        {
            if (bIsInAir)
            {
                ForceStateTransition(ELocomotionState::InAir);
            }
            else
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

    const ELocomotionState OldState = CurrentState;

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
        LandingElapsedTime = 0.f;
        bIsLanding = false;
        bLandingRequested = false;
        bLandingFromFallOff = false;
        bLandingReceivedMoveInput = false;
        LandingPostTouchdownMoveInputTime = 0.f;
        bSuppressFallOffStart = false;
    }

    PreviousState = CurrentState;
    CurrentState = NewState;
    RecordStateControllerDebugEvent(FString::Printf(
        TEXT("ForceStateTransition %s -> %s"),
        *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(PreviousState)),
        *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState))));

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] StateTransition %s -> %s PhysAir=%d AnimAir=%d Input=(R=%.2f,F=%.2f) Ground=%.1f Vertical=%.1f LastFall=%.1f Landing=%d Requested=%d LandTime=%.3f"),
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(OldState)),
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(NewState)),
            bIsPhysicallyInAir ? 1 : 0,
            bIsInAir ? 1 : 0,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            GroundSpeed,
            VerticalSpeed,
            LastFallSpeed,
            bIsLanding ? 1 : 0,
            bLandingRequested ? 1 : 0,
            LandingElapsedTime);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
        LandingElapsedTime = 0.f;
        GetWorld()->GetTimerManager().SetTimer(LandingFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnLandingFallbackTimeout, FMath::Max(0.1f, LandingMaxDuration), false);
    }
}

void ULocomotionAnimStateComponent::RecordStateControllerDebugEvent(const FString& Event)
{
    ++StateControllerDebugEventRevision;
    StateControllerDebugLastEvent = Event;
}

float ULocomotionAnimStateComponent::GetStateControllerDebugLandingFallbackRemaining() const
{
    if (const UWorld* World = GetWorld())
    {
        return World->GetTimerManager().GetTimerRemaining(LandingFallbackTimerHandle);
    }
    return -1.f;
}

bool ULocomotionAnimStateComponent::ConsumeMotionMatchingReselectionRequest()
{
    const bool bRequested = bMotionMatchingReselectionRequested;
    bMotionMatchingReselectionRequested = false;
    return bRequested;
}

bool ULocomotionAnimStateComponent::ConsumeStopPresentationRequest()
{
    const bool bRequested = bStopRequested;
    bStopRequested = false;
    bGroundMoveEpisodeActive = false;
    if (bRequested)
    {
        EmitStopDebug(TEXT("[SC_STOP_COMPONENT] Event=ConsumedByStateController"));
    }
    return bRequested;
}

void ULocomotionAnimStateComponent::RefreshOneShotFallbackTimer(float SelectedAnimationDuration)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const float Duration = FMath::Max(0.1f, SelectedAnimationDuration);
    FTimerManager& TimerManager = World->GetTimerManager();

    switch (CurrentState)
    {
    case ELocomotionState::Start:
        TimerManager.SetTimer(StartFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStartFallbackTimeout, Duration, false);
        break;
    case ELocomotionState::Stop:
        TimerManager.SetTimer(StopFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnStopFallbackTimeout, Duration, false);
        break;
    case ELocomotionState::Landing:
        TimerManager.SetTimer(LandingFallbackTimerHandle, this, &ULocomotionAnimStateComponent::OnLandingFallbackTimeout, Duration, false);
        break;
    case ELocomotionState::InAir:
        if (bIsJumping)
        {
            TimerManager.SetTimer(JumpStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishJumpStart, Duration, false);
        }
        else if (bIsFallOffStart)
        {
            TimerManager.SetTimer(FallOffStartTimerHandle, this, &ULocomotionAnimStateComponent::FinishFallOffStart, Duration, false);
        }
        break;
    default:
        break;
    }
}

void ULocomotionAnimStateComponent::NotifyStartFinished()
{
    // Intentionally empty. Existing animation notifies remain compatible but
    // cannot mutate locomotion/presentation state. Native timers own fallback
    // completion so authored assets no longer affect control flow.
}

void ULocomotionAnimStateComponent::NotifyStopFinished()
{
    // Intentionally empty. See NotifyStartFinished.
}

void ULocomotionAnimStateComponent::NotifyLandingFinished()
{
    // Intentionally empty. Landing completion is driven by native request
    // lifetime/timer policy; anim notifies are informational only.
}

void ULocomotionAnimStateComponent::CompleteLandingFromSelectedAnimation(const TCHAR* CompletionSource)
{
    if (CurrentState == ELocomotionState::Landing)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
        }

        RecordStateControllerDebugEvent(FString::Printf(
            TEXT("Landing completed Source=%s -> MotionMatching Time=%.3f HasInput=%d"),
            CompletionSource,
            LandingElapsedTime,
            bHasMoveInput ? 1 : 0));
        FinishLandingRequest();
    }
}

void ULocomotionAnimStateComponent::OnStartFallbackTimeout()
{
    if (CurrentState == ELocomotionState::Start)
    {
        ForceStateTransition(bHasMoveInput
            ? ELocomotionState::Locomotion
            : ELocomotionState::Stop);
    }
}

void ULocomotionAnimStateComponent::OnStopFallbackTimeout()
{
    if (CurrentState == ELocomotionState::Stop)
    {
        ForceStateTransition(ELocomotionState::Idle);
    }
}

void ULocomotionAnimStateComponent::OnLandingFallbackTimeout()
{
    CompleteLandingFromSelectedAnimation(TEXT("TimerFallback"));
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

    // Capture launch intent before CharacterMovement turns the current input
    // into mid-air steering. JumpStart selection uses this immutable snapshot.
    JumpStartGroundSpeed = 0.f;
    JumpStartMoveDirection = FVector2D::ZeroVector;
    if (CachedBasePlayer)
    {
        if (const UCharacterMovementComponent* MovementComponent = CachedBasePlayer->GetCharacterMovement())
        {
            FVector HorizontalVelocity = MovementComponent->Velocity;
            HorizontalVelocity.Z = 0.f;
            JumpStartGroundSpeed = HorizontalVelocity.Size();

            if (HorizontalVelocity.SizeSquared() > FMath::Square(IdleSpeedThreshold))
            {
                const FVector LocalDirection = CachedBasePlayer->GetActorTransform()
                    .InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
                JumpStartMoveDirection = FVector2D(LocalDirection.Y, LocalDirection.X).GetSafeNormal();
            }
            else
            {
                JumpStartMoveDirection = GetMovementInputForState().GetSafeNormal();
            }
        }
    }
    bJumpStartWasMoving =
        JumpStartGroundSpeed > IdleSpeedThreshold ||
        !JumpStartMoveDirection.IsNearlyZero();

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
    if (CachedBasePlayer && (CachedBasePlayer->HasAuthority() || CachedBasePlayer->IsLocallyControlled()))
    {
        return;
    }

    if (Snapshot.EventSequence < LastRemoteAnimEventSequence)
    {
        return;
    }

    LastFallSpeed = Snapshot.LastFallSpeed;

    bIsSprinting = Snapshot.bIsSprinting;
    bHasMoveInput = Snapshot.bHasMoveInput;
    CachedMoveInput = Snapshot.bHasMoveInput ? Snapshot.MoveInput.GetClampedToMaxSize(1.f) : FVector2D::ZeroVector;
    MoveInput = CachedMoveInput;
    MoveInputSize = CachedMoveInput.Size();
    LandMoveDirection = Snapshot.LandMoveDirection;
}

void ULocomotionAnimStateComponent::HandleLanded(const FHitResult& Hit, float ImpactFallSpeed)
{
    RecordStateControllerDebugEvent(FString::Printf(TEXT("HandleLanded Impact=%.1f"), ImpactFallSpeed));
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

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_FALLOFF] StartFallOffStart! Vel=(%.1f,%.1f,%.1f) Speed=%.1f Accel=(%.1f,%.1f,%.1f) Input=(R=%.2f,F=%.2f) AirTime=%.3f PhysAir=%d Suppress=%d State=%s"),
            Velocity.X, Velocity.Y, Velocity.Z, GroundSpeed, Acceleration.X, Acceleration.Y, Acceleration.Z,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            AirborneDuration,
            bIsPhysicallyInAir ? 1 : 0,
            bSuppressFallOffStart ? 1 : 0,
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)));
        UE_LOG(LogTemp, Warning, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
    if (bIsFallOffStart && IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_FALLOFF] StopFallOffStart! TimeElapsed=%.3f Vel=(%.1f,%.1f,%.1f) Speed=%.1f State=%s"),
            GetWorld() ? GetWorld()->GetTimerManager().GetTimerElapsed(FallOffStartTimerHandle) : -1.f,
            Velocity.X, Velocity.Y, Velocity.Z, GroundSpeed,
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(static_cast<int64>(CurrentState)));
        UE_LOG(LogTemp, Warning, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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

    // Landed can be delivered both by CharacterMovement and by the air-state
    // confirmation path. A second delivery must not rewind the one-shot pose.
    if (CurrentState == ELocomotionState::Landing && bIsLanding && bLandingRequested)
    {
        RecordStateControllerDebugEvent(TEXT("StartLanding ignored: already landing"));
        return;
    }

    const bool bWasJumpLanding = bIsJumping;
    const bool bWasFallOffLanding = bIsFallOffStart;

    World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    StopFallOffStart();
    GroundedConfirmTimer = 0.f;

    if (CachedBasePlayer)
    {
        if (UCharacterMovementComponent* MovementComponent = CachedBasePlayer->GetCharacterMovement())
        {
            Velocity = CachedBasePlayer->GetVelocity();
            FVector HorizontalVelocityForSnapshot = Velocity;
            HorizontalVelocityForSnapshot.Z = 0.f;
            GroundSpeed = HorizontalVelocityForSnapshot.Size();
            VerticalSpeed = Velocity.Z;
            Acceleration = MovementComponent->GetCurrentAcceleration();

            CachedMoveInput = GetMovementInputForState();
            MoveInputSize = CachedMoveInput.Size();
            bHasMoveInput = MoveInputSize > MoveInputDeadZone ||
                (!ShouldUseLocalInput() && GroundSpeed > GenericMoveInputSpeedThreshold);
        }
    }

    LandStartGroundSpeed = GroundSpeed;
    LandStartFallSpeed = ImpactFallSpeed;
    LastFallSpeed = ImpactFallSpeed;
    // If the player released move input before landing, always select Stand Land even with residual velocity
    bLandWasMoving = bHasMoveInput && (LandStartGroundSpeed > IdleSpeedThreshold || Acceleration.SizeSquared2D() > 1.0f);
    bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;

    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.f;
    if (HorizontalVelocity.SizeSquared() > FMath::Square(IdleSpeedThreshold))
    {
        const FVector LocalDirection = CachedBasePlayer->GetActorTransform()
            .InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
        LandMoveDirection = FVector2D(LocalDirection.Y, LocalDirection.X).GetSafeNormal();
        bLandDirectionFromVelocity = true;
    }
    else
    {
        LandMoveDirection = CachedMoveInput.GetSafeNormal();
        bLandDirectionFromVelocity = false;
    }
    LandingStartMoveInput = CachedMoveInput.GetSafeNormal();

    // Sprint landing assets are strongly forward-biased. Walk speed is also exactly 500,
    // so speed alone classified ordinary strafe/back movement as sprint landing.
    // Use sprint landing only when the character was actually sprinting forward.
    constexpr float SprintForwardDirectionThreshold = 0.85f;
    bLandWasSprinting =
        bIsSprinting &&
        LandStartGroundSpeed >= RunToSprintSpeedThreshold &&
        LandMoveDirection.Y >= SprintForwardDirectionThreshold &&
        !IsDiagonalLanding();

    bIsJumping = false;
    bIsInAir = true;
    bIsPhysicallyInAir = false;
    bWasInAir = false;
    bWasAirborneLastFrame = false;
    AirborneDuration = 0.f;
    bSuppressFallOffStart = false;
    bIsLanding = true;
    bLandingRequested = true;
    bLandingFromFallOff = bWasFallOffLanding;
    bCanEnterLand = true;
    bCanEnterGround = false;
    LandingElapsedTime = 0.f;
    LandingStartControlYaw = CachedBasePlayer ? CachedBasePlayer->GetControlRotation().Yaw : 0.f;
    bLandingInputHeldAtTouchdown = bHasMoveInput;
    // A direction held at impact is valid for a later release -> Stop, but it
    // is never a reason by itself to leave the Land animation for MM.
    bLandingReceivedMoveInput = bLandingInputHeldAtTouchdown;
    LandingPostTouchdownMoveInputTime = 0.f;
    LastLandingInputDirectionDelta = 0.f;
    LastLandingControlYawDelta = 0.f;
    bLastLandingInputDirectionChanged = false;
    bLastLandingControlYawChanged = false;

    ForceStateTransition(ELocomotionState::Landing);
    RecordStateControllerDebugEvent(FString::Printf(
        TEXT("StartLanding accepted Impact=%.1f Moving=%d Heavy=%d Input=(%.2f,%.2f)"),
        LandStartFallSpeed,
        bLandWasMoving ? 1 : 0,
        bUseHeavyLand ? 1 : 0,
        CachedMoveInput.X,
        CachedMoveInput.Y));

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] StartLanding Impact=%.1f Ground=%.1f Heavy=%d Moving=%d SprintLand=%d Diagonal=%d MinLandTime=%.3f FromJump=%d FromFallOff=%d Input=(R=%.2f,F=%.2f) LandDir=(R=%.2f,F=%.2f) RealEvent=%d"),
            ImpactFallSpeed,
            LandStartGroundSpeed,
            bUseHeavyLand ? 1 : 0,
            bLandWasMoving ? 1 : 0,
            bLandWasSprinting ? 1 : 0,
            IsDiagonalLanding() ? 1 : 0,
            GetEffectiveMinimumLandingDuration(),
            bWasJumpLanding ? 1 : 0,
            bWasFallOffLanding ? 1 : 0,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            LandMoveDirection.X,
            LandMoveDirection.Y,
            bTriggerRealLandEvent ? 1 : 0);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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

bool ULocomotionAnimStateComponent::IsDiagonalLanding() const
{
    const FVector2D LandingDirection = LandMoveDirection.GetSafeNormal();

    // This must be an impact-time classification.  Using the *current* WASD
    // vector here made a standing land become a diagonal moving-land halfway
    // through its clip, bypassing Start when the player pressed a diagonal key
    // after contact.
    const bool bLandingDirectionDiagonal =
        bLandWasMoving &&
        FMath::Abs(LandingDirection.X) >= SprintDiagonalLandingRightThreshold &&
        FMath::Abs(LandingDirection.Y) >= DiagonalLandingForwardThreshold;

    return bLandingDirectionDiagonal;
}

float ULocomotionAnimStateComponent::GetEffectiveMinimumLandingDuration() const
{
    const bool bIsSimulatedProxy = CachedBasePlayer && CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy;
    float EffectiveDuration = MinimumLandingDuration;

    if (IsDiagonalLanding() && !bIsSimulatedProxy && !bLandingFromFallOff)
    {
        // A diagonal asset needs at least the normal landing window. The old
        // minimum() reduced the default 0.45 s window to 0.16 s and cut the
        // pose before its directional contact had settled.
        EffectiveDuration = FMath::Max(EffectiveDuration, SprintDiagonalLandingDuration);
    }

    if (bIsSimulatedProxy)
    {
        EffectiveDuration = FMath::Max(EffectiveDuration, RemoteMinimumLandingDuration);
    }

    if (bLandingFromFallOff)
    {
        EffectiveDuration = FMath::Max(EffectiveDuration, FallOffMinimumLandingDuration);
    }

    return EffectiveDuration;
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
    if (!bLandingRequested)
    {
        bLandingFromFallOff = false;
    }
}

void ULocomotionAnimStateComponent::FinishLandingRequest()
{
    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] FinishLandingRequest LandTime=%.3f HasInput=%d LandWasMoving=%d Diagonal=%d MinLandTime=%.3f"),
            LandingElapsedTime,
            bHasMoveInput ? 1 : 0,
            bLandWasMoving ? 1 : 0,
            IsDiagonalLanding() ? 1 : 0,
            GetEffectiveMinimumLandingDuration());
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

    bIsLanding = false;
    bLandingRequested = false;
    bIsInAir = false;
    bWasInAir = false;
    bWasAirborneLastFrame = false;
    AirborneDuration = 0.f;
    bSuppressFallOffStart = false;
    bLandingFromFallOff = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;
    RecordStateControllerDebugEvent(FString::Printf(TEXT("FinishLandingRequest Time=%.3f HasInput=%d"), LandingElapsedTime, bHasMoveInput ? 1 : 0));

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
    RecordStateControllerDebugEvent(FString::Printf(
        TEXT("Standing land input -> Start Time=%.3f Input=(%.2f,%.2f)"),
        LandingElapsedTime,
        CachedMoveInput.X,
        CachedMoveInput.Y));
    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] InterruptLandingForMoveInput LandTime=%.3f LandWasMoving=%d MinLandTime=%.3f Input=(R=%.2f,F=%.2f)"),
            LandingElapsedTime,
            bLandWasMoving ? 1 : 0,
            MinimumLandingDuration,
            CachedMoveInput.X,
            CachedMoveInput.Y);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
    bLandingFromFallOff = false;
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

void ULocomotionAnimStateComponent::InterruptLandingForDirectionChange()
{
    const float ControlYawDelta = CachedBasePlayer
        ? FMath::Abs(FMath::FindDeltaAngleDegrees(LandingStartControlYaw, CachedBasePlayer->GetControlRotation().Yaw))
        : 0.f;

    float InputDirectionDelta = 0.f;
    if (!LandingStartMoveInput.IsNearlyZero() && !CachedMoveInput.IsNearlyZero())
    {
        const FVector2D LandDirection = LandingStartMoveInput.GetSafeNormal();
        const FVector2D CurrentInputDirection = CachedMoveInput.GetSafeNormal();
        const float Dot = FMath::Clamp(FVector2D::DotProduct(LandDirection, CurrentInputDirection), -1.f, 1.f);
        InputDirectionDelta = FMath::RadiansToDegrees(FMath::Acos(Dot));
    }

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] InterruptLandingForDirectionChange LandTime=%.3f InputDelta=%.1f ControlYawDelta=%.1f LandDir=(R=%.2f,F=%.2f) Input=(R=%.2f,F=%.2f)"),
            LandingElapsedTime,
            InputDirectionDelta,
            ControlYawDelta,
            LandMoveDirection.X,
            LandMoveDirection.Y,
            CachedMoveInput.X,
            CachedMoveInput.Y);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
    bLandingFromFallOff = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;

    bIsLocomotionTransitioning = true;
    LocomotionTransitionTimer = FMath::Max(LocomotionTransitionTimer, MinTransitionDuration);
    bGroundStartFinished = true;
    bPendingGroundStartFinish = false;
    bUseStartDatabase = false;
    bUseLoopDatabase = true;
    bUseSharpTurnDatabase = false;

    if (bHasMoveInput)
    {
        bGroundMoveEpisodeActive = true;
        bStopRequested = false;
    }

    // The graph is about to expose the MM branch after a direct Land pose.
    // Make its first search use Pose History rather than continuing the
    // locomotion pose that was hidden while the Land Blend Stack owned output.
    bMotionMatchingReselectionRequested = true;

    ForceStateTransition((bHasMoveInput || GroundSpeed > IdleSpeedThreshold)
        ? ELocomotionState::Locomotion
        : ELocomotionState::Idle);
}

void ULocomotionAnimStateComponent::InterruptSprintLandingForSprintRelease()
{
    RecordStateControllerDebugEvent(FString::Printf(
        TEXT("Sprint Land sprint-release -> MotionMatching Time=%.3f Input=(%.2f,%.2f)"),
        LandingElapsedTime,
        CachedMoveInput.X,
        CachedMoveInput.Y));

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] InterruptSprintLandingForSprintRelease LandTime=%.3f Input=(R=%.2f,F=%.2f) Ground=%.1f"),
            LandingElapsedTime,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            GroundSpeed);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
    bLandingFromFallOff = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;

    bIsLocomotionTransitioning = true;
    LocomotionTransitionTimer = FMath::Max(LocomotionTransitionTimer, MinTransitionDuration);
    bGroundStartFinished = true;
    bPendingGroundStartFinish = false;
    bUseStartDatabase = false;
    bUseLoopDatabase = true;
    bUseSharpTurnDatabase = false;

    // The direct Sprint Land was visible while the MM branch was hidden. Its
    // first query must use Pose History and not continue the old hidden pose.
    bMotionMatchingReselectionRequested = true;
    ForceStateTransition(ELocomotionState::Locomotion);
}

void ULocomotionAnimStateComponent::InterruptStartForGaitChange()
{
    RecordStateControllerDebugEvent(FString::Printf(
        TEXT("Start gait change -> MotionMatching Sprint=%d Time=%.3f Input=(%.2f,%.2f)"),
        bIsSprinting ? 1 : 0,
        MoveInputHeldTime,
        CachedMoveInput.X,
        CachedMoveInput.Y));

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] InterruptStartForGaitChange Sprint=%d Input=(R=%.2f,F=%.2f) Ground=%.1f"),
            bIsSprinting ? 1 : 0,
            CachedMoveInput.X,
            CachedMoveInput.Y,
            GroundSpeed);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StartFallbackTimerHandle);
    }

    bGroundStartFinished = true;
    bPendingGroundStartFinish = false;
    bStartRequested = false;
    bUseStartDatabase = false;
    bUseLoopDatabase = true;
    bUseSharpTurnDatabase = false;
    bStartWasSprinting = bIsSprinting;

    // The direct Start was visible while the MM branch was hidden. Its
    // first query must use Pose History and not continue the old hidden pose.
    bMotionMatchingReselectionRequested = true;
    ForceStateTransition(ELocomotionState::Locomotion);
}

void ULocomotionAnimStateComponent::InterruptLandingForStop()
{
    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] InterruptLandingForStop LandTime=%.3f LandWasMoving=%d MinLandTime=%.3f Ground=%.1f"),
            LandingElapsedTime,
            bLandWasMoving ? 1 : 0,
            MinimumLandingDuration,
            GroundSpeed);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }

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
    bStopRequested = true;
    bGroundMoveEpisodeActive = true;
    ForceStateTransition(ELocomotionState::Stop);
}
