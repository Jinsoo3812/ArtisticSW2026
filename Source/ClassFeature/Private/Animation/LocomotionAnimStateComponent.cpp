#include "Animation/LocomotionAnimStateComponent.h"
#include "BasePlayer.h"
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

namespace
{
    bool IsMotionMatchingCaptureEnabled()
    {
        const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MMDebugging"));
        return DebugCVar && DebugCVar->GetInt() > 0;
    }

    void AppendMotionMatchingCaptureLine(const FString& Line)
    {
        const FString LogFilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MMCapture.log"));
        const FString StampedLine = FString::Printf(
            TEXT("[%s] %s%s"),
            *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S.%s")),
            *Line,
            LINE_TERMINATOR);

        FFileHelper::SaveStringToFile(
            StampedLine,
            *LogFilePath,
            FFileHelper::EEncodingOptions::AutoDetect,
            &IFileManager::Get(),
            FILEWRITE_Append);
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
    bIsInAir = false;
    bIsJumping = false;
    bIsLanding = false;
    bUseHeavyLand = false;
    LastFallSpeed = 0.f;
    bIsCombatMode = false;
    bIsTurningInPlace = false;
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
    LandingElapsedTime = 0.f;
    LandingStartControlYaw = 0.f;
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
    FallOffStartMaxDuration = 2.0f;
    LandingMaxDuration = 0.8f;
    RealLandingEventSpeedThreshold = 300.f;
    MinimumLandingDuration = 0.45f;
    LandingDirectionInterruptMinTime = 0.18f;
    LandingInputDirectionInterruptAngle = 45.f;
    LandingControlYawInterruptAngle = 55.f;
    SprintDiagonalLandingDuration = 0.16f;
    SprintDiagonalLandingRightThreshold = 0.25f;
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

    bIsSprinting = CachedBasePlayer->bIsSprinting;

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
    UpdateMaxWalkSpeed();
    UpdateCombatMovementState();

    // ?쒖옄由??뚯쟾(Turn In Place) ?곹깭 癒몄떊 湲곕컲 Desired Rotation ?쒖뼱
    bIsTurningInPlace = (CurrentState == ELocomotionState::TurnInPlace);

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
        bPhysicallyTransitioning = (AngleDiff > 20.f);
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

    // Filter brief ledge/step-down ground contact while falling.
    if (bNowPhysicallyInAir)
    {
        GroundedConfirmTimer = 0.0f;
    }
    else if (bWasInAir)
    {
        // 0.1珥??숈븞 ?곗냽?쇰줈 ?뺤떎?섍쾶 吏?곸뿉 癒몃Ъ?ъ빞 吏꾩쭨 吏???곹깭濡??몄젙
        GroundedConfirmTimer += DeltaTime;
        if (GroundedConfirmTimer < 0.1f)
        {
            bNowInAirForAnimation = true; // ?꾩쭅? 怨듭쨷 ?곹깭 ?좎?
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

    if (bWasInAir && !bNowInAirForAnimation && !bIsLanding && !bLandingRequested)
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

void ULocomotionAnimStateComponent::UpdateCharacterRotation(float DeltaTime)
{
    UCharacterMovementComponent* MovementComponent = CachedBasePlayer ? CachedBasePlayer->GetCharacterMovement() : nullptr;
    if (!MovementComponent) return;

    // Always keep camera-oriented rotation settings (showing back)
    MovementComponent->bOrientRotationToMovement = false;

    // Keep idle/stop poses from rotating with camera-only look input.
    MovementComponent->bUseControllerDesiredRotation =
        CurrentState != ELocomotionState::Idle &&
        CurrentState != ELocomotionState::Stop &&
        CurrentState != ELocomotionState::TurnInPlace;

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

    const bool bCanSprint =
        bIsSprinting &&
        !CachedBasePlayer->bIsAttacking &&
        !CachedBasePlayer->bIsDodging &&
        !CachedBasePlayer->bIsHitReacting;

    float TargetRotationRate = bCanSprint ? SprintRotationRateYaw : WalkRotationRateYaw;

    MovementComponent->MaxWalkSpeed = bCanSprint ? SprintSpeed : WalkSpeed;
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
    if (!bUseInstantRotationSnap)
    {
        return;
    }

    if (!CachedBasePlayer ||
        (!CachedBasePlayer->IsLocallyControlled() && !CachedBasePlayer->HasAuthority()))
    {
        return;
    }

    const float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
    const float ControlYaw = CachedBasePlayer->GetControlRotation().Yaw;
    const float YawDelta = FMath::FindDeltaAngleDegrees(ActorYaw, ControlYaw);
    if (FMath::Abs(YawDelta) < StartAlignControlYawThreshold)
    {
        return;
    }

    FRotator NewRotation = CachedBasePlayer->GetActorRotation();
    NewRotation.Yaw = ControlYaw;
    CachedBasePlayer->SetActorRotation(NewRotation);

    // Apply negative yaw offset to mesh so it visually stays at the old rotation
    MeshYawOffset -= YawDelta;
    MeshYawOffset = FRotator::NormalizeAxis(MeshYawOffset);

    if (USkeletalMeshComponent* Mesh = CachedBasePlayer->GetMesh())
    {
        FRotator MeshRot = DefaultMeshRelativeRotation;
        MeshRot.Yaw += MeshYawOffset;
        Mesh->SetRelativeRotation(MeshRot);
    }

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] AlignStartYaw ActorYaw=%.1f ControlYaw=%.1f Delta=%.1f Threshold=%.1f Input=(R=%.2f,F=%.2f)"),
            ActorYaw,
            ControlYaw,
            YawDelta,
            StartAlignControlYawThreshold,
            CachedMoveInput.X,
            CachedMoveInput.Y);
        UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
        AppendMotionMatchingCaptureLine(DebugLine);
    }
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
            else if (bLandWasSprinting &&
                bHasMoveInput &&
                LandingElapsedTime >= SprintDiagonalLandingDuration &&
                CachedMoveInput.Y > 0.15f &&
                (FMath::Abs(CachedMoveInput.X) >= SprintDiagonalLandingRightThreshold ||
                    FMath::Abs(LandMoveDirection.X) >= SprintDiagonalLandingRightThreshold))
            {
                if (IsMotionMatchingCaptureEnabled())
                {
                    const FString DebugLine = FString::Printf(
                        TEXT("[MMCAP_EVENT] FinishSprintDiagonalLanding LandTime=%.3f Input=(R=%.2f,F=%.2f) LandDir=(R=%.2f,F=%.2f) ShortLandTime=%.3f"),
                        LandingElapsedTime,
                        CachedMoveInput.X,
                        CachedMoveInput.Y,
                        LandMoveDirection.X,
                        LandMoveDirection.Y,
                        SprintDiagonalLandingDuration);
                    UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
                    AppendMotionMatchingCaptureLine(DebugLine);
                }
                FinishLandingRequest();
            }
            else if (LandingElapsedTime >= LandingDirectionInterruptMinTime)
            {
                bool bInputDirectionChanged = false;
                if (bHasMoveInput && !LandMoveDirection.IsNearlyZero() && !CachedMoveInput.IsNearlyZero())
                {
                    const FVector2D LandDirection = LandMoveDirection.GetSafeNormal();
                    const FVector2D CurrentInputDirection = CachedMoveInput.GetSafeNormal();
                    const float Dot = FMath::Clamp(FVector2D::DotProduct(LandDirection, CurrentInputDirection), -1.f, 1.f);
                    const float DirectionDelta = FMath::RadiansToDegrees(FMath::Acos(Dot));
                    bInputDirectionChanged = DirectionDelta >= LandingInputDirectionInterruptAngle;
                }

                bool bControlYawChanged = false;
                if (CachedBasePlayer && (bHasMoveInput || bLandWasMoving || GroundSpeed > IdleSpeedThreshold))
                {
                    const float ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
                        LandingStartControlYaw,
                        CachedBasePlayer->GetControlRotation().Yaw));
                    bControlYawChanged = ControlYawDelta >= LandingControlYawInterruptAngle;
                }

                if (bInputDirectionChanged || bControlYawChanged)
                {
                    InterruptLandingForDirectionChange();
                }
                else if (LandingElapsedTime >= MinimumLandingDuration)
                {
                    if (!bLandWasMoving)
                    {
                        if (bHasMoveInput)
                        {
                            InterruptLandingForMoveInput();
                        }
                    }
                    else // bLandWasMoving
                    {
                        if (bHasMoveInput)
                        {
                            // Moving land should finish into locomotion, not restart with run_Start.
                            FinishLandingRequest();
                        }
                        else
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
        bSuppressFallOffStart = false;
    }

    PreviousState = CurrentState;
    CurrentState = NewState;

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
        if (LandingElapsedTime < MinimumLandingDuration)
        {
            if (UWorld* World = GetWorld())
            {
                const float RemainingLandingTime = FMath::Max(0.01f, MinimumLandingDuration - LandingElapsedTime);
                World->GetTimerManager().SetTimer(
                    LandingFallbackTimerHandle,
                    this,
                    &ULocomotionAnimStateComponent::NotifyLandingFinished,
                    RemainingLandingTime,
                    false);
            }

            if (IsMotionMatchingCaptureEnabled())
            {
                const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] DelayLandingFinished LandTime=%.3f MinLandTime=%.3f"),
                    LandingElapsedTime,
                    MinimumLandingDuration);
                UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
                AppendMotionMatchingCaptureLine(DebugLine);
            }
            return;
        }

        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(LandingFallbackTimerHandle);
        }
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
    if (CachedBasePlayer && (CachedBasePlayer->HasAuthority() || CachedBasePlayer->IsLocallyControlled()))
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
    LandMoveDirection = Snapshot.LandMoveDirection;
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

    const bool bWasJumpLanding = bIsJumping;
    const bool bWasFallOffLanding = bIsFallOffStart;

    World->GetTimerManager().ClearTimer(JumpStartTimerHandle);
    StopFallOffStart();
    GroundedConfirmTimer = 0.f;

    LandStartGroundSpeed = GroundSpeed;
    LandStartFallSpeed = ImpactFallSpeed;
    LastFallSpeed = ImpactFallSpeed;
    bLandWasMoving = LandStartGroundSpeed > IdleSpeedThreshold || bHasMoveInput;
    bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;

    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.f;
    if (HorizontalVelocity.SizeSquared() > FMath::Square(IdleSpeedThreshold))
    {
        const FVector LocalDirection = CachedBasePlayer->GetActorTransform()
            .InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
        LandMoveDirection = FVector2D(LocalDirection.Y, LocalDirection.X).GetSafeNormal();
    }
    else
    {
        LandMoveDirection = CachedMoveInput.GetSafeNormal();
    }

    // Sprint landing assets are strongly forward-biased. Walk speed is also exactly 500,
    // so speed alone classified ordinary strafe/back movement as sprint landing.
    // Use sprint landing only when the character was actually sprinting forward.
    constexpr float SprintForwardDirectionThreshold = 0.35f;
    bLandWasSprinting =
        bIsSprinting &&
        LandStartGroundSpeed >= RunToSprintSpeedThreshold &&
        LandMoveDirection.Y >= SprintForwardDirectionThreshold;

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
    LandingElapsedTime = 0.f;
    LandingStartControlYaw = CachedBasePlayer ? CachedBasePlayer->GetControlRotation().Yaw : 0.f;

    ForceStateTransition(ELocomotionState::Landing);

    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(
            TEXT("[MMCAP_EVENT] StartLanding Impact=%.1f Ground=%.1f Heavy=%d Moving=%d SprintLand=%d FromJump=%d FromFallOff=%d Input=(R=%.2f,F=%.2f) LandDir=(R=%.2f,F=%.2f) RealEvent=%d"),
            ImpactFallSpeed,
            LandStartGroundSpeed,
            bUseHeavyLand ? 1 : 0,
            bLandWasMoving ? 1 : 0,
            bLandWasSprinting ? 1 : 0,
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
    if (IsMotionMatchingCaptureEnabled())
    {
        const FString DebugLine = FString::Printf(TEXT("[MMCAP_EVENT] FinishLandingRequest LandTime=%.3f HasInput=%d LandWasMoving=%d MinLandTime=%.3f"),
            LandingElapsedTime,
            bHasMoveInput ? 1 : 0,
            bLandWasMoving ? 1 : 0,
            MinimumLandingDuration);
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
    if (!LandMoveDirection.IsNearlyZero() && !CachedMoveInput.IsNearlyZero())
    {
        const FVector2D LandDirection = LandMoveDirection.GetSafeNormal();
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

    ForceStateTransition((bHasMoveInput || GroundSpeed > IdleSpeedThreshold)
        ? ELocomotionState::Locomotion
        : ELocomotionState::Idle);
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
    bSuppressFallOffStart = false;
    bCanEnterLand = false;
    bCanEnterGround = true;
    LastFallSpeed = 0.f;

    ForceStateTransition(ELocomotionState::Stop);
}
