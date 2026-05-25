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
    Combat
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

    // AnimNotify callback functions
    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyStartFinished();

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyStopFinished();

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Animation")
    void NotifyLandingFinished();

    // Setters called from input system
    void SetMoveInput(float Right, float Forward);
    void ClearMoveInput();

    // Triggered on jump start
    void HandleJumpStarted();

    // Triggered on landing event from character movement
    void HandleLanded(const FHitResult& Hit);

    // Forces immediate state transition
    void ForceStateTransition(ELocomotionState NewState);

    // Backward-compatibility stubs for ABasePlayer
    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void MarkGroundStartFinished() { NotifyStartFinished(); }

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void FinishJumpStart() {}

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Stubs")
    void FinishFallOffStart() {}

protected:
    void CacheOwner();
    bool PerformGroundProbe() const;
    void UpdateStateTransitions(float DeltaTime);
    
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

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    bool bIsInAir;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Air")
    bool bIsJumping;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bIsLanding;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    bool bUseHeavyLand;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Landing")
    float LastFallSpeed;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Combat")
    bool bIsCombatMode;

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

    // Timers
    FTimerHandle StartFallbackTimerHandle;
    FTimerHandle StopFallbackTimerHandle;
    FTimerHandle LandingFallbackTimerHandle;

protected:
    UPROPERTY(Transient)
    TObjectPtr<ABasePlayer> CachedBasePlayer;

    // Track simulated proxy airborne duration
    float AirborneDuration;
    bool bWasAirborneLastFrame;
    FVector2D PreviousMoveInput;
};
