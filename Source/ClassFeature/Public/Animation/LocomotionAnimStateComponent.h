#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocomotionAnimStateComponent.generated.h"

class ABasePlayer;
class ACharacter;

UENUM(BlueprintType)
enum class ELocomotionState : uint8
{
    Idle,
    Start,
    Locomotion,
    Stop,
    InAir,
    Landing,
    TurnInPlace,
    Combat
};

UENUM(BlueprintType)
enum class EStateControllerPresentationState : uint8
{
    None = 0 UMETA(DisplayName = "None"),
    IdleLoop = 1 UMETA(DisplayName = "Idle Loop"),
    TransitionToStart = 2 UMETA(DisplayName = "Transition To Start"),
    LocomotionLoop = 3 UMETA(DisplayName = "Locomotion Loop"),
    TransitionToStop = 4 UMETA(DisplayName = "Transition To Stop"),
    TransitionToPivot = 5 UMETA(DisplayName = "Transition To Pivot"),
    TransitionToJump = 6 UMETA(DisplayName = "Transition To Jump"),
    TransitionToLand = 7 UMETA(DisplayName = "Transition To Land"),
    TurnInPlace = 8 UMETA(DisplayName = "Turn In Place")
};

UENUM(BlueprintType)
enum class EMovementDirection : uint8
{
    Forward = 0 UMETA(DisplayName = "Forward (F)"),
    ForwardLeft = 1 UMETA(DisplayName = "Forward Left (FL)"),
    Left = 2 UMETA(DisplayName = "Left (L)"),
    BackwardLeft = 3 UMETA(DisplayName = "Backward Left (BL)"),
    Backward = 4 UMETA(DisplayName = "Backward (B)"),
    BackwardRight = 5 UMETA(DisplayName = "Backward Right (BR)"),
    Right = 6 UMETA(DisplayName = "Right (R)"),
    ForwardRight = 7 UMETA(DisplayName = "Forward Right (FR)")
};

UENUM(BlueprintType)
enum class EGaitIntent : uint8
{
    Walk = 0 UMETA(DisplayName = "Walk"),
    Run = 1 UMETA(DisplayName = "Run"),
    Sprint = 2 UMETA(DisplayName = "Sprint")
};

USTRUCT(BlueprintType)
struct FStateControllerContextSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState CurrentPresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState DesiredPresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bShouldTurnInPlace = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float DesiredFacingDeltaYaw = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceRootYawDelta = 0.0f;
};

UENUM(BlueprintType)
enum class EReplicatedLocomotionEvent : uint8
{
    None,
    Jump,
    FallOff,
    Landed
};

USTRUCT(BlueprintType)
struct FReplicatedLocomotionState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    bool bIsSprinting = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    bool bHasMoveInput = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    FVector2D MoveInput = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    FVector2D LandMoveDirection = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    float LandStartGroundSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    float LastFallSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    int32 EventSequence = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Network")
    EReplicatedLocomotionEvent LastLocomotionEvent = EReplicatedLocomotionEvent::None;

    bool operator==(const FReplicatedLocomotionState& Other) const
    {
        return bIsSprinting == Other.bIsSprinting &&
               bHasMoveInput == Other.bHasMoveInput &&
               MoveInput.Equals(Other.MoveInput, 0.05f) &&
               LandMoveDirection.Equals(Other.LandMoveDirection, 0.05f) &&
               FMath::IsNearlyEqual(LandStartGroundSpeed, Other.LandStartGroundSpeed) &&
               FMath::IsNearlyEqual(LastFallSpeed, Other.LastFallSpeed) &&
               EventSequence == Other.EventSequence &&
               LastLocomotionEvent == Other.LastLocomotionEvent;
    }

    bool operator!=(const FReplicatedLocomotionState& Other) const
    {
        return !(*this == Other);
    }
};

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API ULocomotionAnimStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULocomotionAnimStateComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Updates the locomotion state machine
    void UpdateAnimationState(float DeltaTime);

    // Legacy AnimNotify callback functions. These are intentionally inert:
    // State Controller one-shots are completed by native request/clock policy
    // and fallback timers, never by animation asset notifies.
    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyStartFinished();

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyStopFinished();

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyLandingFinished();

    /** Consumes a pending input-release Stop after State Controller has
     * committed the corresponding direct Blend Stack one-shot. */
    bool ConsumeStopPresentationRequest();

    // Setters called from input system
    void SetMoveInput(float Right, float Forward);
    void ClearMoveInput();

    // Triggered on jump start
    void HandleJumpStarted();
    void HandleRemoteJumpStarted(int32 EventSequence);
    void ApplyAuthoritativeSnapshot(const FReplicatedLocomotionState& Snapshot);

    // Triggered on landing event from character movement
    void HandleLanded(const FHitResult& Hit, float ImpactFallSpeed);
    void HandleRemoteFallOffStarted(int32 EventSequence);
    void HandleRemoteLanded(float ImpactFallSpeed, int32 EventSequence);

    void SetSprinting(bool bNewSprinting);

    // Forces immediate state transition
    void ForceStateTransition(ELocomotionState NewState);

    /**
     * Re-arms the currently active one-shot safety timer to the duration of the
     * animation selected by the State Controller.  This is only a fallback;
     * an authored notify can still finish the state earlier.
     */
    void RefreshOneShotFallbackTimer(float SelectedAnimationDuration);

    // Backward-compatibility stubs for ABasePlayer
    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void MarkGroundStartFinished() { NotifyStartFinished(); }

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Rotation")
    bool GetUseInstantRotationSnap() const { return bUseInstantRotationSnap; }

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Rotation")
    float GetMeshYawOffset() const { return MeshYawOffset; }

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void FinishJumpStart();

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void FinishFallOffStart();

    /** Event history consumed by the State Controller diagnostic trace. */
    int32 GetStateControllerDebugEventRevision() const { return StateControllerDebugEventRevision; }
    const FString& GetStateControllerDebugLastEvent() const { return StateControllerDebugLastEvent; }
    bool GetStateControllerDebugIsDiagonalLanding() const { return IsDiagonalLanding(); }
    float GetStateControllerDebugEffectiveMinimumLandingDuration() const { return GetEffectiveMinimumLandingDuration(); }
    float GetStateControllerDebugLandingFallbackRemaining() const;

    /**
     * One-shot request consumed by the primary AnimInstance when a Land is
     * deliberately redirected to Motion Matching.  The MM node must discard
     * its continuing search pose and build the first query from Pose History;
     * otherwise a hidden/stale MM branch visibly pops when the graph bool
     * switches away from the Land Blend Stack.
     */
    bool ConsumeMotionMatchingReselectionRequest();

protected:
    void CacheOwner();
    bool PerformGroundProbe() const;
    bool IsDedicatedServer() const;
    bool IsInAirForAnimation() const;
    bool ShouldUseLocalInput() const;
    FVector2D GetMovementInputForState() const;
    void UpdateStateTransitions(float DeltaTime);
    void UpdateAirState(float DeltaTime);
    void UpdateMovementRequestState(float DeltaTime);
    void UpdateCombatMovementState();
    /** Project_J-style derived stationary Strafe TIP phase. This component is
     * the sole owner of the request consumed by State Controller/Chooser. */
    void UpdateTurnInPlacePhase(float DeltaTime);
    void UpdateCharacterRotation(float DeltaTime);
    void UpdateMaxWalkSpeed() const;
    void ClearMovementRequests();
    void AlignActorYawToControlYawForStartIfNeeded();
    void StartFallOffStart();
    void StopFallOffStart();
    void StartLanding(float ImpactFallSpeed, bool bTriggerRealLandEvent);
    void FinishLanding();
    void FinishLandingRequest();
    void CompleteLandingFromSelectedAnimation(const TCHAR* CompletionSource);
    void InterruptLandingForMoveInput();
    void InterruptLandingForDirectionChange();
    void InterruptLandingForStop();
    void RecordStateControllerDebugEvent(const FString& Event);
    bool ShouldAcceptRemoteAnimEvent(int32 EventSequence);
    bool IsDiagonalLanding() const;
    float GetEffectiveMinimumLandingDuration() const;
    
    // Timer fallback functions
    void OnStartFallbackTimeout();
    void OnStopFallbackTimeout();
    void OnLandingFallbackTimeout();

public:
    // Core Locomotion variables
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ELocomotionState CurrentState;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ELocomotionState PreviousState;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float GroundSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float VerticalSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Acceleration;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Velocity;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Input")
    bool bHasMoveInput;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Input")
    float MoveInputSize;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Input")
    FVector2D MoveInput;


    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Transitions")
    bool bSharpTurnRequested;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Transitions")
    bool bStartRequested;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Transitions")
    bool bStopRequested;

    /**
     * Latches a locally controlled grounded movement episode until its Stop
     * presentation has been committed.  Input is written by the movement
     * layer before this component updates, so bPrevHasMoveInput alone can
     * miss the release edge on a frame where input is cleared early.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Transitions")
    bool bGroundMoveEpisodeActive = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    bool bIsInAir;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    bool bIsJumping;

    /** Snapshot at the accepted jump event. Live air input must not reclassify JumpStart. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    bool bJumpStartWasMoving = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    float JumpStartGroundSpeed = 0.f;

    /** Character-local launch direction: X=right, Y=forward. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    FVector2D JumpStartMoveDirection = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bIsLanding;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bUseHeavyLand;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    float LastFallSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Combat")
    bool bIsCombatMode;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsTurningInPlace = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bShouldTurnInPlace = false;

    /** Project_J-style derived TIP phase. It outlives the raw entry threshold
     * so an authored turn root track can finish and reselect cleanly. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bTurnInPlacePhaseActive = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float TurnInPlacePhaseElapsed = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float DesiredFacingDeltaYaw = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float TurnInPlaceRootYawDelta = 0.0f;

    /** Increments only when an active TIP receives a deliberate additional turn input. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    int32 TurnInPlaceContinuationSerial = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsLocomotionTransitioning = false;

    // Backward-compatibility properties for ABasePlayer
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bIsPhysicallyInAir;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bIsFallOffStart;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bLandingRequested;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bCanEnterLand;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bCanEnterGround;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bPrevHasMoveInput;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    FVector2D CachedMoveInput;

    UPROPERTY(BlueprintReadWrite, Category = "Locomotion|Compatibility")
    bool bIsSprinting;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float MoveInputHeldTime;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float CurrentStartToLoopDelay;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bUseStartDatabase;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bGroundStartFinished;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bPendingGroundStartFinish;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bStartWasSprinting;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bUseLoopDatabase;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bUseSharpTurnDatabase;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float MoveInputTurnAngle;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    FVector2D PreviousMoveInputForTurn;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float LandStartGroundSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float LandStartFallSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bLandWasMoving;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    bool bLandWasSprinting;

    /** Character-local landing movement direction: X=right, Y=forward. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    FVector2D LandMoveDirection;

    /** True when LandMoveDirection came from impact velocity rather than input. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bLandDirectionFromVelocity = false;

    /** Input direction captured at impact. Kept separate from velocity so camera-relative steering does not invalidate a landing spuriously. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    FVector2D LandingStartMoveInput;

    /**
     * True only after movement intent has survived touchdown for
     * LandingExitStopInputHoldTime.  This deliberately excludes input that was
     * merely held in the air and released on the landing frame.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bLandingReceivedMoveInput = false;

    /** Input that was already held at the precise touchdown frame.  Holding
     * the same direction must not be treated as a new Land redirect. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bLandingInputHeldAtTouchdown = false;

    /** Accumulates real move intent after touchdown for the Land -> Stop contract. */
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    float LandingPostTouchdownMoveInputTime = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    float LandingElapsedTime;

    // Last evaluated redirect gates.  They are diagnostic snapshots only; the
    // actual landing behaviour continues to use the values evaluated in Tick.
    float LastLandingInputDirectionDelta = 0.f;
    float LastLandingControlYawDelta = 0.f;
    bool bLastLandingInputDirectionChanged = false;
    bool bLastLandingControlYawChanged = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    float LandingStartControlYaw = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bLandingFromFallOff = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Fallback")
    float StartMaxDuration = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Fallback")
    float StopMaxDuration = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Fallback")
    float JumpStartMaxDuration = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Fallback")
    float FallOffStartMaxDuration = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Fallback")
    float LandingMaxDuration = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing")
    float RealLandingEventSpeedThreshold = 300.f;

    /** Prevents held/released movement input from cancelling the landing pose immediately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0"))
    float MinimumLandingDuration = 0.45f;

    /**
     * Project_J-style grace time: only an input held for this long after
     * touchdown may cancel a moving Land directly into Stop.  A release before
     * this point leaves the authored Land pose intact and prevents Land -> Stop
     * blend flashes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0", ClampMax = "0.25", Units = "s"))
    float LandingExitStopInputHoldTime = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0"))
    float RemoteMinimumLandingDuration = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0"))
    float FallOffMinimumLandingDuration = 0.60f;

    /** Allows deliberate steering to leave landing earlier without snapping immediately on impact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0"))
    float LandingDirectionInterruptMinTime = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float LandingInputDirectionInterruptAngle = 45.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float LandingControlYawInterruptAngle = 55.f;

    /** Legacy lower bound for diagonal landings. Diagonal landings never finish before MinimumLandingDuration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0"))
    float SprintDiagonalLandingDuration = 0.16f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SprintDiagonalLandingRightThreshold = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Landing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DiagonalLandingForwardThreshold = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Sprint")
    float WalkSpeed = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Sprint")
    float SprintSpeed = 700.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Sprint")
    float WalkRotationRateYaw = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Sprint")
    float SprintRotationRateYaw = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float StartAlignControlYawThreshold = 90.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Input")
    float GenericMoveInputSpeedThreshold = 3.f;


    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float MovementDirection;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float CombatInputForward;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float CombatInputRight;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float CombatForwardSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Compatibility")
    float CombatRightSpeed;

    // Tuning constants
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float IdleSpeedThreshold;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float SharpTurnAngleThreshold;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float SharpTurnMinSpeed;

    /** A direct Pivot is a high-commitment reversal. Smaller turns stay in the
     *  Motion Matching transition PSD. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float PivotAngleThreshold;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float PivotMinSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float HeavyLandSpeedThreshold;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float MoveInputDeadZone;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float StopIntentSpeedThreshold = 80.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float RunToSprintSpeedThreshold = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float MoveInputTurnDeadZoneAngle = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float MinTransitionDuration = 0.45f;

    // Timers
    FTimerHandle StartFallbackTimerHandle;
    FTimerHandle StopFallbackTimerHandle;
    FTimerHandle LandingFallbackTimerHandle;
    FTimerHandle FallOffStartTimerHandle;
    FTimerHandle JumpStartTimerHandle;

protected:
    /** true일 경우 즉각 캡슐 회전을 스냅하고 메쉬 오프셋 보간을 적용합니다. false일 경우 PSD의 Reface 애니메이션이 매칭되도록 스냅하지 않습니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start")
    bool bUseInstantRotationSnap = false;

    UPROPERTY(Transient)
    float MeshYawOffset = 0.f;

    UPROPERTY(Transient)
    FRotator DefaultMeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning")
    float MeshYawOffsetInterpSpeed = 10.f;

    UPROPERTY(Transient)
    TObjectPtr<ABasePlayer> CachedBasePlayer;

    // Track simulated proxy airborne duration
    float AirborneDuration;
    bool bWasAirborneLastFrame;
    FVector2D PreviousMoveInput;

    float LocomotionTransitionTimer;
    bool bWasInAir = false;
    bool bSuppressFallOffStart = false;
    int32 LastRemoteAnimEventSequence = 0;
    float GroundedConfirmTimer = 0.0f;
    int32 StateControllerDebugEventRevision = 0;
    FString StateControllerDebugLastEvent;
    bool bMotionMatchingReselectionRequested = false;
};
