#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "GameplayTagContainer.h"
#include "SwimmingComponent.h"
#include "MotionMatchingAnimInstance.generated.h"

class UPoseSearchDatabase;
class UChooserTable;
class UCharacterTrajectoryComponent;
class UAnimationAsset;
class FStructProperty;
class FObjectProperty;
class FFloatProperty;

UENUM(BlueprintType)
enum class EWeaponUpperBodyOverlayMode : uint8
{
    None,
    BowIdle,
    BowRun,
    BowSprint
};

UENUM(BlueprintType)
enum class EWeaponUpperBodyOverlayState : uint8
{
    None,
    Idle,
    Run,
    Sprint
};

/** The foot that should initiate a one-shot locomotion transition. */
UENUM(BlueprintType)
enum class EStateControllerOneShotFoot : uint8
{
    Left,
    Right
};

USTRUCT(BlueprintType)
struct FAnimMovementData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Velocity = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector VelocityLocal = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Acceleration = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FTransformTrajectory Trajectory;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FTransformTrajectory FallOffTrajectoryBefore;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float FallOffElapsedTime = 0.f;
};

USTRUCT(BlueprintType)
struct FAnimInputData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float MoveInputSize = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bHasMoveInput = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bSharpTurnRequested = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector2D MoveInput = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FAnimGroundData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bStartRequested = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bStopRequested = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint8 GroundMotionMode = 0;
};

USTRUCT(BlueprintType)
struct FAnimAirData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsInAir = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsJumping = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsFallOffStart = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bJumpStartWasMoving = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float JumpStartGroundSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector2D JumpStartMoveDirection = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FAnimLandingData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsLanding = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bUseHeavyLand = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float LastFallSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float GroundSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float VerticalSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float LandStartGroundSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float LandStartFallSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float LandingElapsedTime = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector2D LandMoveDirection = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsPhysicallyInAir = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bLandingRequested = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bLandWasMoving = false;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bLandWasSprinting = false;
};

USTRUCT(BlueprintType)
struct FAnimAimData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "AimOffset")
    float AimYaw = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "AimOffset")
    float AimPitch = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "AimOffset")
    float AimOffsetAlpha = 0.f;
};

USTRUCT(BlueprintType)
struct FAnimWeaponUpperBodyData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    bool bHasWeaponEquipped = false;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    FGameplayTag EquippedWeaponTag;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    FGameplayTag OverlayTag;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    int32 OverlayIndex = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    bool bShouldOverrideUpperBody = false;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    EWeaponUpperBodyOverlayState OverlayState = EWeaponUpperBodyOverlayState::None;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    float UpperBodyAlpha = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    float GroundSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    float Direction = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    bool bIsSprinting = false;
};

USTRUCT(BlueprintType)
struct FAnimBowData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bIsAiming = false;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bIsDrawing = false;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bIsFullyDrawn = false;

    // Becomes true slightly before the draw montage exits so the full-draw pose
    // is already underneath the montage when it blends out.
    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bShouldUseFullDrawPose = false;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bIsReleasing = false;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    float DrawAlpha = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    bool bHasStringIKTarget = false;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    float StringIKAlpha = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    FTransform StringIKTargetTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FS_ChooserOutputs
{
    GENERATED_BODY()

    /** Per-row playback offset, written by a Chooser struct-output column. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser", meta = (ClampMin = "0.0", Units = "s"))
    float StartTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
    bool bUseMM = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser", meta = (ClampMin = "0.0"))
    float MMCostLimit = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser", meta = (ClampMin = "0.0", Units = "s"))
    float BlendTime = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
    FName BlendProfile = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
    FGameplayTagContainer Tags;
};

USTRUCT(BlueprintType)
struct FAnimStateControllerThreadSafeData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState PresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EMovementDirection MovementDirection = EMovementDirection::Forward;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EMovementDirection PreviousMovementDirection = EMovementDirection::Forward;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    TObjectPtr<UAnimationAsset> SelectedAnimation = nullptr;

    /** Full authored Chooser metadata cached beside SelectedAnimation (Project_J/GASP contract). */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    FS_ChooserOutputs SelectedAnimationOutput;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float SelectedAnimationBlendTime = 0.2f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float SelectedAnimationStartTime = 0.0f;

    /** Playback time in the direct Blend Stack one-shot, before StartTime is applied. */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float SelectedAnimationElapsedTime = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bSelectedAnimationShouldLoop = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bHasSelectedAnimation = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    int32 SelectionRevision = 0;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bShouldOverrideMotionMatching = false;

    /**
     * Raised only for an intentional Land redirect.  It forces the next MM
     * query to use the outgoing graph Pose History instead of a continuing
     * search result that was accumulated behind the direct Land pose.
     */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bForceMotionMatchingReselection = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceSteeringAlpha = 0.0f;

    /**
     * GASP-style steering gate for the current Blend Stack clip.  This is
     * intentionally separate from authored Turn In Place rotation: only a
     * moving/in-air character may steer root motion.
     */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float BlendStackSteeringAlpha = 0.0f;

    /** Future (0.5 s) trajectory facing used by the generic Steering node. */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    FRotator BlendStackSteeringTargetOrientation = FRotator::ZeroRotator;

    /**
     * Local (actor-relative) movement angle for the current direct one-shot.
     * This deliberately comes from the one-shot direction latch when one is
     * active; live velocity becomes zero before a Land -> Stop hand-off.
     */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float CombatStateOrientationWarpingAngle = 0.0f;

    /** Authored curve still decides the final weight; this is the state gate. */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float CombatStateOrientationWarpingAlpha = 0.0f;

    /** Velocity-to-acceleration turn angle; diagnostics only, never a direct Pivot trigger. */
    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TrajectoryTurnAngleDegrees = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceRootYawDelta = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bEnableAO = true;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    FVector2D AOValue = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FAnimThreadSafeData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FAnimMovementData MovementData;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FAnimInputData InputData;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FAnimGroundData GroundData;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FAnimAirData AirData;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FAnimLandingData LandingData;

    UPROPERTY(BlueprintReadOnly, Category = "AimOffset")
    FAnimAimData AimData;

    UPROPERTY(BlueprintReadOnly, Category = "Weapon UpperBody")
    FAnimWeaponUpperBodyData WeaponUpperBodyData;

    UPROPERTY(BlueprintReadOnly, Category = "Bow")
    FAnimBowData BowData;

    UPROPERTY(BlueprintReadOnly, Category = "Swimming")
    FSwimmingAnimationState SwimData;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    FAnimStateControllerThreadSafeData StateController;
};

struct FCachedMotionMatchingNodeInfo
{
    FStructProperty* NodeProperty = nullptr;
    FObjectProperty* DatabaseProperty = nullptr;
    FFloatProperty* SearchThrottleTimeProperty = nullptr;
    FBoolProperty* ShouldSearchProperty = nullptr;
    FBoolProperty* ResetOnBecomingRelevantProperty = nullptr;
    FProperty* NextUpdateInterruptModeProperty = nullptr;
    TObjectPtr<UPoseSearchDatabase> AppliedDatabase = nullptr;
    TWeakObjectPtr<const UObject> LastSelectedAnim;
    float LastSelectedTime = 0.f;
    float DefaultSearchThrottleTime = 0.f;
    bool bDefaultSearchThrottleCached = false;
    int32 DefaultMaxActiveBlends = 4;
    bool bDefaultMaxActiveBlendsCached = false;
    TWeakObjectPtr<const UObject> PreUpdateSelectedAnim;
    TWeakObjectPtr<const UObject> PreUpdateSelectedDatabase;
    TWeakObjectPtr<const UObject> PreUpdateStackTopAnim;
    float PreUpdateSelectedTime = 0.f;
    float PreUpdateStackTopTime = 0.f;
    int32 PreUpdateStackNum = 0;
    bool bPreUpdateContinue = false;
    bool bPreUpdateDbChanged = false;
    bool bPreUpdateAppliedDbChanged = false;
    float PreUpdateThrottle = 0.f;
    int32 PreUpdateMaxActiveBlends = 0;
    bool bPreUpdateShouldSearch = false;
    bool bPostUpdateRestoredTransitionStack = false;
    bool bPostUpdateCollapsedTransitionStack = false;
    TWeakObjectPtr<const UObject> LastStackTopAnim;
    float LastStackTopTime = 0.f;
    int32 LastStackNum = 0;
    TWeakObjectPtr<UAnimationAsset> LockedRemoteTransitionAnim;
    float LockedRemoteTransitionTime = 0.f;
    ELocomotionState LockedRemoteTransitionState = ELocomotionState::Idle;
    bool bHasRemoteTransitionLock = false;
    TWeakObjectPtr<UAnimationAsset> LockedJumpStartAnim;
    float LockedJumpStartTime = 0.f;
    bool bHasJumpStartLock = false;
};

struct FCachedHistoryCollectorNodeInfo
{
    FStructProperty* NodeProperty = nullptr;
    FStructProperty* TrajectoryProperty = nullptr;
};

USTRUCT(BlueprintType)
struct FMotionMatchingAnimInstanceProxy : public FAnimInstanceProxy
{
    GENERATED_BODY()

public:
    FMotionMatchingAnimInstanceProxy();
    FMotionMatchingAnimInstanceProxy(UAnimInstance* InAnimInstance);

    virtual void UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InGroupRelevancyName) override;

    UPROPERTY(Transient)
    FAnimThreadSafeData ThreadSafeData;

    UPROPERTY(Transient)
    TObjectPtr<UPoseSearchDatabase> CurrentActivePoseSearchDatabase;

    TArray<FCachedMotionMatchingNodeInfo> CachedMMNodes;
    TArray<FCachedHistoryCollectorNodeInfo> CachedHistoryNodes;
    bool bNodesCached = false;
    float DebugLogAccumulator = 0.f;

    void CacheNodes(UAnimInstance* InAnimInstance);
};

UCLASS(Blueprintable, BlueprintType)
class CLASSFEATURE_API UMotionMatchingAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

    friend struct FMotionMatchingAnimInstanceProxy;

public:
    UMotionMatchingAnimInstance();

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativePostEvaluateAnimation() override;

    /**
     * The main AnimInstance calls this for linked animation-layer instances.
     * Linked instances own a separate proxy, so swim state must be copied to
     * that proxy instead of relying on their update order.
     */
    void ReceiveLinkedSwimAnimationState(const FSwimmingAnimationState& InSwimState);

    UFUNCTION(BlueprintPure, Category = "Motion Matching", meta = (BlueprintThreadSafe))
    UPoseSearchDatabase* GetCurrentActivePoseSearchDatabaseThreadSafe() const;

    UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
    float GetThreadSafeAimYaw() const;

    UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
    float GetThreadSafeAimPitch() const;

    UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
    float GetThreadSafeAimOffsetAlpha() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    bool GetThreadSafeHasBowEquipped() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    bool GetThreadSafeHasWeaponEquipped() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    FGameplayTag GetThreadSafeEquippedWeaponTag() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    FGameplayTag GetThreadSafeWeaponUpperBodyOverlayTag() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    int32 GetThreadSafeWeaponUpperBodyOverlayIndex() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    bool GetThreadSafeShouldOverrideWeaponUpperBody() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    EWeaponUpperBodyOverlayMode GetThreadSafeWeaponUpperBodyMode() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    EWeaponUpperBodyOverlayState GetThreadSafeWeaponUpperBodyState() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    float GetThreadSafeWeaponUpperBodyAlpha() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    float GetThreadSafeWeaponUpperBodySpeed() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Weapon UpperBody", meta = (BlueprintThreadSafe))
    float GetThreadSafeWeaponUpperBodyDirection() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsBowAiming() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsBowDrawing() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsBowFullyDrawn() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeShouldUseBowFullDrawPose() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    float GetThreadSafeBowHoldAimOffsetAlpha() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsBowReleasing() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    float GetThreadSafeBowDrawAlpha() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    bool GetThreadSafeHasBowStringIKTarget() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    float GetThreadSafeBowStringIKAlpha() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Bow", meta = (BlueprintThreadSafe))
    FTransform GetThreadSafeBowStringIKTargetTransform() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsSwimming() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsUnderwater() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    bool GetThreadSafeSwimDiveInputHeld() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    bool GetThreadSafeSwimAscendInputHeld() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    ESwimDepthMode GetThreadSafeSwimDepthMode() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    float GetThreadSafeSwimSpeed() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    float GetThreadSafeSwimVerticalSpeed() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Swimming", meta = (BlueprintThreadSafe))
    float GetThreadSafeSwimDirection() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
    FFootPlacementPlantSettings Get_FootPlacementPlantSettings() const;

    UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
    FFootPlacementInterpolationSettings Get_FootPlacementInterpolationSettings() const;

    // State Controller ThreadSafe Getters for AnimGraph
    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    EStateControllerPresentationState GetThreadSafeStateControllerPresentationState() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    EMovementDirection GetThreadSafeStateControllerMovementDirection() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    UAnimationAsset* GetThreadSafeStateControllerSelectedAnimation() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerSelectedAnimationBlendTime() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerSelectedAnimationStartTime() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerSelectedAnimationElapsedTime() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    bool GetThreadSafeStateControllerSelectedAnimationShouldLoop() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    bool GetThreadSafeStateControllerHasSelectedAnimation() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    int32 GetThreadSafeStateControllerSelectionRevision() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    bool GetThreadSafeShouldOverrideMotionMatching() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const;

    /** GASP-compatible generic steering alpha. Prefer this name for new AnimGraph wiring. */
    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerBlendStackSteeringAlpha() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerCombatStateOrientationWarpingAngle() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerCombatStateOrientationWarpingAlpha() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    FRotator GetThreadSafeStateControllerDesiredFacingRotator() const;

    /** GASP-compatible generic Steering target (the predicted trajectory facing at +0.5 s). */
    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    FRotator GetThreadSafeStateControllerBlendStackSteeringTargetOrientation() const;

    /** Offset Root Bone modes: keep translation centered; only TIP may retain rotational offset. */
    UFUNCTION(BlueprintPure, Category = "Offset Root", meta = (BlueprintThreadSafe))
    EOffsetRootBoneMode GetThreadSafeOffsetRootRotationMode() const;

    UFUNCTION(BlueprintPure, Category = "Offset Root", meta = (BlueprintThreadSafe))
    EOffsetRootBoneMode GetThreadSafeOffsetRootTranslationMode() const;

    UFUNCTION(BlueprintPure, Category = "Offset Root", meta = (BlueprintThreadSafe))
    float GetThreadSafeOffsetRootTranslationHalfLife() const;

    UFUNCTION(BlueprintPure, Category = "Offset Root", meta = (BlueprintThreadSafe))
    float GetThreadSafeOffsetRootTranslationRadius() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    FVector2D GetThreadSafeAOValue() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    bool GetThreadSafeEnableAO() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    EStateControllerPresentationState StateControllerPresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    EMovementDirection StateControllerMovementDirection = EMovementDirection::Forward;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    EGaitIntent StateControllerGait = EGaitIntent::Run;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    float StateControllerSpeed2D = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    float StateControllerDesiredFacingDeltaYaw = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsHeavyLand = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsMovingLand = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsInAir = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsJumping = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsFallOff = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    EMovementDirection StateControllerPreviousMovementDirection = EMovementDirection::Forward;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerIsPivoting = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    bool bStateControllerShouldTurnInPlace = false;

    /** Latched when entering a one-shot state; expose this property as a Chooser enum column. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    EStateControllerOneShotFoot StateControllerOneShotFoot = EStateControllerOneShotFoot::Left;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    EGaitIntent GetThreadSafeGait() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    float GetThreadSafeSpeed2D() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    float GetThreadSafeDesiredFacingDeltaYaw() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsHeavyLand() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsMovingLand() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsInAir() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsJumping() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsFallOff() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    EMovementDirection GetThreadSafeStateControllerPreviousMovementDirection() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeIsPivoting() const;

    UFUNCTION(BlueprintPure, Category = "StateController|Chooser", meta = (BlueprintThreadSafe))
    bool GetThreadSafeShouldTurnInPlace() const;

    FStructProperty* CachedTrajectoryProperty = nullptr;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

    void PropagateSwimAnimationStateToLinkedInstances(const FSwimmingAnimationState& InSwimState);

    FSwimmingAnimationState LinkedSwimAnimationState;
    bool bHasLinkedSwimAnimationState = false;

    bool IsDedicatedServerAnimationContext() const;
    float CalculateAimOffsetAlpha(const FAnimThreadSafeData& ThreadSafeData) const;

protected:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<ABasePlayer> CachedBasePlayer;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<ULocomotionAnimStateComponent> CachedLocomotionStateComponent;

    // Master Chooser Table for State Controller
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> MainChooserTable;

    // Optional Sub-Chooser Tables (Fallbacks if MainChooserTable is not assigned)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> StartChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> StopChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> LandChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> InAirChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> PivotChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> TurnInPlaceChooserTable;

    /** Amount reserved at the end of a land one-shot before Motion Matching resumes. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StateController|Landing", meta = (ClampMin = "0.0", Units = "s"))
    float StateControllerLandCompletionLeadTime = 0.05f;

    /** Start is cancellable when the player clearly changes move or facing intent. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StateController|Start", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float StateControllerStartInputInterruptAngle = 12.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StateController|Start", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float StateControllerStartControlYawInterruptAngle = 15.0f;

    // State Controller Runtime Playback Hold Data
    EStateControllerPresentationState StateControllerPlaybackHoldState = EStateControllerPresentationState::None;
    /** Current gameplay presentation request, before the one-shot hold policy is applied. */
    EStateControllerPresentationState StateControllerRequestedPresentationState = EStateControllerPresentationState::None;
    float StateControllerPlaybackHoldElapsed = 0.0f;
    float StateControllerPlaybackHoldDuration = 0.0f;
    FVector2D StateControllerStartMoveInput = FVector2D::ZeroVector;
    float StateControllerStartControlYaw = 0.0f;
    bool bStateControllerStartInputChanged = false;
    bool bStateControllerStartControlYawChanged = false;
    float StateControllerStartInputDeltaDegrees = 0.0f;
    float StateControllerStartControlYawDeltaDegrees = 0.0f;
    TObjectPtr<UAnimationAsset> StateControllerSelectedAnimation = nullptr;
    FS_ChooserOutputs StateControllerSelectedAnimationOutput;
    float StateControllerSelectedAnimationBlendTime = 0.2f;
    float StateControllerSelectedAnimationStartTime = 0.0f;
    bool bStateControllerSelectedAnimationShouldLoop = false;
    int32 StateControllerSelectionRevision = 0;

    // Foot-contact values are captured after animation evaluation, then latched at the next one-shot entry.
    UPROPERTY(EditDefaultsOnly, Category = "StateController|Foot Phase")
    FName StateControllerLeftFootContactCurveName = TEXT("contact_l");

    UPROPERTY(EditDefaultsOnly, Category = "StateController|Foot Phase")
    FName StateControllerRightFootContactCurveName = TEXT("contact_r");

    UPROPERTY(EditDefaultsOnly, Category = "StateController|Foot Phase", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float StateControllerFootContactDifferenceThreshold = 0.2f;

    UPROPERTY(EditDefaultsOnly, Category = "StateController|Foot Phase")
    EStateControllerOneShotFoot StateControllerNoPhaseFootFallback = EStateControllerOneShotFoot::Left;

    float CachedStateControllerLeftFootContact = 0.0f;
    float CachedStateControllerRightFootContact = 0.0f;
    bool bHasStateControllerFootContactCurves = false;
    bool bHasStateControllerFootPhaseHistory = false;
    EStateControllerOneShotFoot StateControllerFootPhaseHistory = EStateControllerOneShotFoot::Left;

    // Movement Direction & Quadrant Thresholds
    EMovementDirection CurrentMovementDirection = EMovementDirection::Forward;
    EMovementDirection MovementDirectionLastFrame = EMovementDirection::Forward;

    // Land Gait Lock
    EGaitIntent StateControllerLandGaitLock = EGaitIntent::Walk;
    bool bHasStateControllerLandGaitLock = false;
    /** The impact direction remains meaningful after velocity has reached zero.
     *  It is consumed only by the immediate Land -> Stop one-shot hand-off. */
    EMovementDirection StateControllerLandingDirectionLatch = EMovementDirection::Forward;
    bool bHasStateControllerLandingDirectionLatch = false;
    /** Exact world-space impact facing (not only the quantized eight-way sector).
     *  Land and its immediate Stop hand-off use this as their common Steering target. */
    float StateControllerLandingSteeringTargetYaw = 0.0f;
    /** Same impact direction as above, expressed in actor-local degrees for Orientation Warping. */
    float StateControllerLandingOrientationWarpingAngle = 0.0f;

    void EvaluateStateControllerPresentationState();
    void EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState);
    /** Emits event-driven diagnostics for direct Chooser one-shots and TIP rotation. */
    void EmitStateControllerDebugTrace(const FAnimThreadSafeData& ThreadSafeData);
    EStateControllerOneShotFoot ResolveStateControllerOneShotFoot(bool bAllowPhaseHistoryFallback) const;
    void UpdateMovementDirection();
    void CalculateAOValueAndEnableAO();

    // Motion Matching PSD assets (Direct C++ selection)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> IdleDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionTransitionDatabase;

    // Sprint uses only a sustained Motion Matching database. Its start/stop
    // one-shots are selected by the State Controller Choosers.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintLocomotionDatabase;

    // Only the sustained air loop belongs to Motion Matching. Jump, fall-off,
    // land, TIP, start and stop are direct State Controller Chooser one-shots.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> InAirDatabase;

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> CurrentActivePoseSearchDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    bool bForceAimOffsetAlwaysOn = true;

    // The authored draw, full-draw, and release poses own the arms. A global aim offset
    // applied after them would otherwise rotate those poses again.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (DisplayName = "Suppress Aim Offset While Bow Draw Pose Is Active"))
    bool bSuppressAimOffsetWhileBowFullyDrawn = true;

    // Disabled by default: the authored bow animation already places hand_r on the string.
    // Enable only as a small per-character correction after validating the bow grip alignment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Bow|IK")
    bool bEnableBowStringHandIK = false;

    // Starts preparing the static full-draw upper-body pose before the draw
    // montage's final blend-out. This avoids exposing the normal bow idle pose.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Bow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FullDrawPosePreloadAlpha = 0.9f;

    // Separate from the global aim offset. This is consumed only by the bow
    // layer while the full-draw hold pose is active.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Bow|AimOffset")
    bool bEnableBowHoldAimOffset = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Bow|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BowHoldAimOffsetAlpha = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float MaxAimYaw = 90.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float MaxAimPitch = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float StandingAimAlpha = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float MovingAimAlpha = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float SprintAimAlpha = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float CombatAimAlpha = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset")
    float GenericMoveInputSpeedThreshold = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Weapon UpperBody")
    bool bEnableWeaponUpperBodyOverlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Weapon UpperBody", meta = (ClampMin = "0.0"))
    float WeaponUpperBodyMovingSpeedThreshold = 80.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Weapon UpperBody")
    bool bForceSprintWeaponUpperBodyDirectionForward = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
    FFootPlacementPlantSettings FootPlacementPlantSettingsDefault;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
    FFootPlacementPlantSettings FootPlacementPlantSettingsStops;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
    FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsDefault;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
    FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsStops;



protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization")
    bool bSkipDedicatedServerAnimationDataUpdate = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float HiddenRemoteUpdateInterval = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float RecentlyRenderedTolerance = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float NearMotionMatchingDistance = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float MidMotionMatchingDistance = 6000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float FarMotionMatchingDistance = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float MidMotionMatchingUpdateInterval = 0.033f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Optimization", meta = (ClampMin = "0.0"))
    float FarMotionMatchingUpdateInterval = 0.083f;

    bool ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds);

private:
    float MotionMatchingUpdateAccumulator = 0.0f;

    ELocomotionState LastState = ELocomotionState::Idle;
    FName LastDatabaseName = NAME_None;
    float StateLogTimer = 0.0f;

    // State-controller diagnostics are intentionally event-driven.  These keep
    // a small amount of game-thread history so normal locomotion never floods
    // the output log while a.StateControllerDebug is enabled.
    EStateControllerPresentationState LastStateControllerDebugPresentation = EStateControllerPresentationState::None;
    int32 LastStateControllerDebugSelectionRevision = INDEX_NONE;
    int32 LastStateControllerDebugComponentEventRevision = INDEX_NONE;
    float LastStateControllerDebugActorYaw = 0.0f;
    double NextStateControllerTurnInPlaceDebugTime = 0.0;
    double NextStateControllerOneShotDebugTime = 0.0;
    double NextStateControllerPivotDebugTime = 0.0;
    bool bHasStateControllerDebugActorYaw = false;

    /** Debug-only: records the Main -> SubChooser chain selected for the current one-shot. */
    FString StateControllerLastChooserPath;
    /** Debug-only: captures StartTime immediately after each parent/child Chooser evaluation. */
    FString StateControllerLastChooserOutputTrace;

    bool bWasFallOffForDebug = false;
    float FallOffDebugElapsedTime = 0.0f;
    bool bSuppressDatabaseSearchThreadSafe = false;
    TObjectPtr<UPoseSearchDatabase> LockedTransitionDatabase = nullptr;
    bool bTransitionLocked = false;
    ELocomotionState LockedTransitionState = ELocomotionState::Idle;
};
