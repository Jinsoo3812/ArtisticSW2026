#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
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

    // Motion Matching PSD assets (Direct C++ selection)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> IdleDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> TurnInPlaceDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> StartDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> StartDatabaseRemote;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> RunStartRefaceDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionDatabaseRemote;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionTransitionDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> LocomotionTransitionDatabaseRemote;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> StopDatabase;

    // Sprint PSDs
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintStartDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintStartDatabaseRemote;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintStartRefaceDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintLocomotionDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Sprint")
    TObjectPtr<UPoseSearchDatabase> SprintStopDatabase;

    // Air / Landing PSDs
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> JumpStartDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> InAirDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> FallOffDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> StandLandLightDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> StandLandHeavyDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> RunLandLightDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> RunLandHeavyDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> SprintLandLightDatabase;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Air")
    TObjectPtr<UPoseSearchDatabase> SprintLandHeavyDatabase;

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
    bool bWasFallOffForDebug = false;
    float FallOffDebugElapsedTime = 0.0f;
    bool bSuppressDatabaseSearchThreadSafe = false;
    TObjectPtr<UPoseSearchDatabase> LockedTransitionDatabase = nullptr;
    bool bTransitionLocked = false;
    ELocomotionState LockedTransitionState = ELocomotionState::Idle;
};
