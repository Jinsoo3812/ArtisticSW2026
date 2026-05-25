#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "PoseSearch/PoseSearchTrajectoryTypes.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "MotionMatchingAnimInstance.generated.h"

class UPoseSearchDatabase;
class UChooserTable;
class UCharacterTrajectoryComponent;

USTRUCT(BlueprintType)
struct FAnimMovementData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Velocity = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FVector Acceleration = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    FPoseSearchQueryTrajectory Trajectory;
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
};

UCLASS(Blueprintable, BlueprintType)
class CLASSFEATURE_API UMotionMatchingAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UMotionMatchingAnimInstance();

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

    bool IsDedicatedServerAnimationContext() const;

protected:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<ABasePlayer> CachedBasePlayer;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<ULocomotionAnimStateComponent> CachedLocomotionStateComponent;

    // Chooser Table Asset
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching")
    TObjectPtr<UChooserTable> ChooserTable;

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching")
    TObjectPtr<UPoseSearchDatabase> CurrentActivePoseSearchDatabase;

    // Pre-processed Chooser Boolean columns
    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Chooser")
    bool bChooserUseRunStart;

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Chooser")
    bool bChooserUseRunStop;

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Chooser")
    bool bChooserIsInAir;
};
