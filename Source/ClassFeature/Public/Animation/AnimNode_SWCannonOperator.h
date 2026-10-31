#pragma once
#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/CannonOperatorSpineSolver.h"
#include "AnimNode_SWCannonOperator.generated.h"

/** Final pose overlay. Targets are sampled on the game thread; no actor reads in evaluation. */
USTRUCT(BlueprintInternalUseOnly)
struct CLASSFEATURE_API FAnimNode_SWCannonOperator : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()
	FAnimNode_SWCannonOperator();
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="25")) float PelvisDrop = 12.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="25")) float ForwardLean = 12.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="55")) float HipLean = 35.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="40")) float HipForward = 20.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="50")) float KneeWidth = 36.f;
	/** Finger flexion in degrees, around each reference finger's anatomical bend axis. */
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="85")) float FingerCurl = 55.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="60")) float MaxTorsoYaw = 25.f;
	/** Fraction of the rear contact bearing used for trunk twist. */
	UPROPERTY(EditAnywhere, Category="Operator|Aim", meta=(ClampMin="0", ClampMax="1")) float TorsoYawFollow = .35f;
	UPROPERTY(EditAnywhere, Category="Operator|Aim", meta=(ClampMin="0", ClampMax="60")) float MaxSideLean = 45.f;
	UPROPERTY(EditAnywhere, Category="Operator|Reach", meta=(ClampMin="0", ClampMax="40")) float MaxPelvisForwardShift = 35.f;
	UPROPERTY(EditAnywhere, Category="Operator|Reach", meta=(ClampMin="0", ClampMax="40")) float MaxPelvisSideShift = 30.f;
	UPROPERTY(EditAnywhere, Category="Operator|Reach", meta=(ClampMin="0", ClampMax="45")) float MaxReachCrouch = 40.f;
	UPROPERTY(EditAnywhere, Category="Operator|Reach", meta=(ClampMin="0", ClampMax="60")) float ShoulderAboveGrip = 48.f;
	/** Head gaze only: zero looks along the bore, one looks at the palm contacts. */
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="1")) float GripLookWeight = .25f;
	/** Rest bend preference. The reach solver distributes flexion across five spine joints. */
	UPROPERTY(EditAnywhere, Category="Operator|Waist", meta=(ClampMin="0", ClampMax="20")) float ExtraWaistBend = 6.f;
	UPROPERTY(EditAnywhere, Category="Operator|Waist", meta=(ClampMin="0", ClampMax="20")) float TurnWaistBend = 8.f;
	UPROPERTY(EditAnywhere, Category="Operator|Waist", meta=(ClampMin="15", ClampMax="120")) float MaxWaistBend = 70.f;
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="90")) float MaxHeadAimCorrection = 80.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="30")) float MaxTorsoPitch = 0.f;
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="80")) float MaxHeadYaw = 45.f;
	/** Neck limits are measured relative to the already posed chest, not the camera. */
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="70")) float MaxHeadPitchUp = 35.f;
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="70")) float MaxHeadPitchDown = 50.f;
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="30")) float HeadAimFollowSpeed = 8.f;
	UPROPERTY(EditAnywhere, Category="Operator|Head", meta=(ClampMin="0", ClampMax="360")) float HeadAimDegreesPerSecond = 120.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="0", ClampMax="25")) float FootHeight = 12.f;
	/** Account for each mesh's ankle-to-sole distance; FootHeight remains a manual fallback. */
	UPROPERTY(EditAnywhere, Category="Operator") bool bUseReferenceFootHeight = true;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="1")) float BlendSpeed = 8.f;
	UPROPERTY(EditAnywhere, Category="Operator", meta=(ClampMin="1")) float BlendOutSpeed = 4.f;
	/** Mesh-specific correction relative to the authored palm contact frame. */
	UPROPERTY(EditAnywhere, Category="Operator") FRotator LeftWristOffset = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Category="Operator") FRotator RightWristOffset = FRotator::ZeroRotator;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCannonOperatorLifecycleTest;
#endif
	FBoneReference Pelvis, LowerSpine, MidSpine, Spine, Head;
	FBoneReference UpperSpine[2];
	FBoneReference Limb[4][3];
	FBoneReference Fingers[2][5][3];
	FQuat GripRotations[2];
	FVector Targets[4], Poles[4];
	FVector Up = FVector::UpVector, Right = FVector::RightVector;
	float HeadYaw = 0.f, HeadPitch = 0.f;
	FSWCannonSpineState ReachSolution, ReachApplied;
	bool bReachInitialized=false;
	FQuat HeadCorrection = FQuat::Identity;
	bool bHeadAimInitialized=false;
	double LastHeadAimSampleTime=-1.;
	double LastReachSampleTime=-1.;
	float MountWeight = 0.f;
	double LastSampleTime = -1.;
	double SampleTime = 0., LastEvaluatedSampleTime = -1.;
	float SampleDeltaTime = 0.f;
	bool bCachedMounted = false;
	TArray<FTransform> PreviousOutputLocal, PreviousOutputCS, PreviousInputCS;
	bool bHasTargets = false;
};
