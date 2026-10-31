#include "Animation/AnimNode_SWCannonOperator.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Cannon.h"
#include "TwoBoneIK.h"
#include "SwimmingComponent.h"
#include "Animation/CannonOperatorHeadAim.h"

FAnimNode_SWCannonOperator::FAnimNode_SWCannonOperator()
{
	Pelvis.BoneName = TEXT("pelvis"); Spine.BoneName = TEXT("spine_03"); Head.BoneName=TEXT("head");
	LowerSpine.BoneName=TEXT("spine_01"); MidSpine.BoneName=TEXT("spine_02");
	UpperSpine[0].BoneName=TEXT("spine_04"); UpperSpine[1].BoneName=TEXT("spine_05");
	const TCHAR* Names[4][3] = {{TEXT("thigh_l"),TEXT("calf_l"),TEXT("foot_l")},
		{TEXT("thigh_r"),TEXT("calf_r"),TEXT("foot_r")},
		{TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l")},
		{TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r")}};
	for (int32 I=0; I<4; ++I) for (int32 J=0; J<3; ++J) Limb[I][J].BoneName = Names[I][J];
	const TCHAR* FingerNames[]={TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")};
	for (int32 Side=0; Side<2; ++Side) for (int32 Finger=0; Finger<5; ++Finger) for (int32 Joint=0; Joint<3; ++Joint)
		Fingers[Side][Finger][Joint].BoneName=FName(*FString::Printf(TEXT("%s_0%d_%s"),FingerNames[Finger],Joint+1,Side==0 ? TEXT("l") : TEXT("r")));
	Alpha = 0.f;
}

void FAnimNode_SWCannonOperator::PreUpdate(const UAnimInstance* Instance)
{
	const USkeletalMeshComponent* Mesh = Instance ? Instance->GetSkelMeshComponent() : nullptr;
	const ACharacter* Character = Mesh ? Cast<ACharacter>(Mesh->GetOwner()) : nullptr;
	const ACannon* Cannon = Character ? Cast<ACannon>(Character->GetAttachParentActor()) : nullptr;
	if (Mesh && Mesh->IsSimulatingPhysics())
	{
		bCachedMounted=false; bReachInitialized=false; bHeadAimInitialized=false;
		Alpha=0.f; MountWeight=0.f; bHasTargets=false; LastSampleTime=-1.; return;
	}
	const bool Mounted = Cannon && Cannon->GetRidingPlayer() == Character && !Mesh->IsSimulatingPhysics();
	const UCharacterMovementComponent* Movement=Character ? Character->GetCharacterMovement() : nullptr;
	const bool Swimming=Movement && (Movement->IsSwimming()
		|| (Movement->MovementMode==MOVE_Custom && Movement->CustomMovementMode==uint8(ECustomMovementMode::CMOVE_Swimming)));
	// Once released, live combat and movement own the pose immediately. The cosmetic
	// return-to-idle blend must never delay a montage, a jump, or a swim transition.
	if (!Mounted && Character && (Instance->IsAnyMontagePlaying()
		|| Character->bPressedJump || Swimming
		|| !Character->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero()))
	{
		bCachedMounted=false; bReachInitialized=false; bHeadAimInitialized=false;
		Alpha=0.f; MountWeight=0.f; bHasTargets=false; LastSampleTime=-1.; return;
	}
	const UWorld* World=Mesh ? Mesh->GetWorld() : nullptr;
	const double Now=World ? World->GetTimeSeconds() : 0.;
	// Autonomous network moves may update a mesh more than once in one world
	// frame. Integrate presentation time once, rather than reusing world DeltaSeconds.
	const float Dt=LastSampleTime>=0. ? FMath::Max(0.f,static_cast<float>(Now-LastSampleTime)) : (World ? World->GetDeltaSeconds() : 0.f);
	LastSampleTime=Now;
	const float Step=FMath::Min(Dt,.1f), Speed=FMath::Max(1.f,BlendSpeed);
	if (Mounted && !bCachedMounted) { bReachInitialized=false; bHeadAimInitialized=false; }
	SampleTime=Now; SampleDeltaTime=Step; bCachedMounted=Mounted;
	MountWeight = FMath::FInterpTo(MountWeight, Mounted ? 1.f : 0.f, Step, Mounted ? Speed : FMath::Max(1.f,BlendOutSpeed));
	// Blend the pose and end-effector goals ourselves, then solve IK at full
	// strength. A final FK alpha blend would lift planted feet between poses.
	Alpha = MountWeight>1.e-4f ? 1.f : 0.f;
	if (!Mounted) return;
	for (int32 I=0; I<4; ++I) if (!Cannon->GetOperatorTarget(I)) return;
	const FTransform TM = Mesh->GetComponentTransform();
	Up = TM.InverseTransformVectorNoScale(Character->GetActorUpVector()).GetSafeNormal();
	Right = TM.InverseTransformVectorNoScale(Character->GetActorRightVector()).GetSafeNormal();
	for (int32 I=0; I<4; ++I)
	{
		Targets[I] = TM.InverseTransformPosition(Cannon->GetOperatorTarget(I)->GetComponentLocation());
		if (I>=2) GripRotations[I-2]=(TM.GetRotation().Inverse()*Cannon->GetOperatorTarget(I)->GetComponentQuat()).GetNormalized();
		if (I<2 && !bUseReferenceFootHeight) Targets[I] += Up * FootHeight;
		const float PoleWidth=I<2 ? KneeWidth : 40.f;
		const FVector PoleLocal(I<2 ? 80.f : -35.f, I%2 ? PoleWidth : -PoleWidth, I<2 ? -35.f : 30.f);
		Poles[I] = TM.InverseTransformPosition(Character->GetActorTransform().TransformPosition(PoleLocal));
	}
	const FVector2D Aim = Cannon->GetVisualOperatorAim();
	const float LookYaw=FMath::FindDeltaAngleDegrees(Character->GetActorRotation().Yaw,
		Cannon->GetActorRotation().Yaw + static_cast<float>(Aim.X));
	// The firing direction is only an optional head-look target. Spine reach is
	// driven by the actual moving rear grip positions in Evaluate.
	HeadYaw = FMath::FInterpTo(HeadYaw,LookYaw,Step,Speed);
	HeadPitch = FMath::FInterpTo(HeadPitch,-static_cast<float>(Aim.Y),Step,Speed);
	bHasTargets = true;
}

void FAnimNode_SWCannonOperator::InitializeBoneReferences(const FBoneContainer& Bones)
{
	Pelvis.Initialize(Bones); Spine.Initialize(Bones); Head.Initialize(Bones);
	LowerSpine.Initialize(Bones); MidSpine.Initialize(Bones);
	for (auto& Bone:UpperSpine) Bone.Initialize(Bones);
	for (auto& Chain : Limb) for (auto& Bone : Chain) Bone.Initialize(Bones);
	for (auto& Hand : Fingers) for (auto& Finger : Hand) for (auto& Bone : Finger) Bone.Initialize(Bones);
}

bool FAnimNode_SWCannonOperator::IsValidToEvaluate(const USkeleton*, const FBoneContainer& Bones)
{
	if (!bHasTargets || !Pelvis.IsValidToEvaluate(Bones) || !Spine.IsValidToEvaluate(Bones) || !Head.IsValidToEvaluate(Bones)) return false;
	if (!LowerSpine.IsValidToEvaluate(Bones) || !MidSpine.IsValidToEvaluate(Bones)) return false;
	for (const auto& Bone:UpperSpine) if (!Bone.IsValidToEvaluate(Bones)) return false;
	for (const auto& Chain : Limb) for (const auto& Bone : Chain) if (!Bone.IsValidToEvaluate(Bones)) return false;
	for (const auto& Hand : Fingers) for (const auto& Finger : Hand) for (const auto& Bone : Finger) if (!Bone.IsValidToEvaluate(Bones)) return false;
	return true;
}

void FAnimNode_SWCannonOperator::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& Out)
{
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const int32 Count = Output.Pose.GetPose().GetNumBones();
	// A deterministic mounted base prevents locomotion/MM choices from changing
	// limb reach on different peers. End-effectors are constrained after pose blending.
	FCompactPose Reference;
	Reference.SetBoneContainer(&Bones);
	Reference.ResetToRefPose();
	FCSPose<FCompactPose> ReferenceCS;
	ReferenceCS.InitPose(MoveTemp(Reference));
	TArray<FTransform> CS, Local, InputCS;
	TArray<int32> Parents;
	CS.SetNum(Count); Local.SetNum(Count); InputCS.SetNum(Count); Parents.SetNum(Count);
	for (int32 I=0; I<Count; ++I)
	{
		const FCompactPoseBoneIndex Index(I);
		CS[I] = ReferenceCS.GetComponentSpaceTransform(Index);
		InputCS[I] = Output.Pose.GetComponentSpaceTransform(Index);
		Parents[I] = Bones.GetParentBoneIndex(Index).GetInt();
		Local[I] = Parents[I] >= 0 ? CS[I].GetRelativeTransform(CS[Parents[I]]) : CS[I];
	}
	// Rebuild descendants after each modification, including twist bones and fingers.
	const auto SetCS = [&](int32 Index, const FTransform& Value)
	{
		CS[Index] = Value;
		Local[Index] = Parents[Index]>=0 ? Value.GetRelativeTransform(CS[Parents[Index]]) : Value;
		for (int32 J=Index+1; J<Count; ++J)
		{
			int32 P=Parents[J];
			while (P>Index) P=Parents[P];
			if (P==Index) CS[J]=Local[J]*CS[Parents[J]];
		}
	};
	const int32 PelvisIndex=Pelvis.GetCompactPoseIndex(Bones).GetInt();
	const FVector Forward=FVector::CrossProduct(Right,Up).GetSafeNormal();
	FVector Goals[4]={Targets[0],Targets[1],Targets[2],Targets[3]};
	FVector JointPoles[4]={Poles[0],Poles[1],Poles[2],Poles[3]};
	if (bUseReferenceFootHeight) for (int32 I=0; I<2; ++I)
		Goals[I]+=Up*FVector::DotProduct(Up,ReferenceCS.GetComponentSpaceTransform(Limb[I][2].GetCompactPoseIndex(Bones)).GetLocation());
	FQuat HandRotations[2];
	FVector PalmBack[2];
	for (int32 Side=0; Side<2; ++Side)
	{
		const FTransform RefHand=ReferenceCS.GetComponentSpaceTransform(Limb[Side+2][2].GetCompactPoseIndex(Bones));
		const auto Knuckle=[&](int32 Finger) { return RefHand.InverseTransformPosition(ReferenceCS.GetComponentSpaceTransform(Fingers[Side][Finger][0].GetCompactPoseIndex(Bones)).GetLocation()); };
		const FVector Along=Knuckle(2).GetSafeNormal();
		PalmBack[Side]=(FVector::CrossProduct(Along,Knuckle(4)-Knuckle(1))*(Side==0 ? -1.f : 1.f)).GetSafeNormal();
		const FQuat RefFrame=FRotationMatrix::MakeFromXZ(Along,PalmBack[Side]).ToQuat();
		HandRotations[Side]=(GripRotations[Side]*RefFrame.Inverse()*(Side==0 ? LeftWristOffset : RightWristOffset).Quaternion()).GetNormalized();
		// The authored target is a palm contact frame, not a wrist floating on the surface.
		const FVector PalmCenter=(Knuckle(1)+Knuckle(2)+Knuckle(3)+Knuckle(4))*.1375f;
		Goals[Side+2]-=HandRotations[Side].RotateVector(PalmCenter);
	}
	FSWCannonSpineRig Rig;
	Rig.Forward=Forward; Rig.Right=Right; Rig.Up=Up;
	const FVector BaseShift=-Up*FMath::Clamp(PelvisDrop,0.f,25.f)+Forward*HipForward;
	Rig.Hip=ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(PelvisIndex));Rig.Hip.AddToTranslation(BaseShift);
	const int32 SpineIndices[]={LowerSpine.GetCompactPoseIndex(Bones).GetInt(),MidSpine.GetCompactPoseIndex(Bones).GetInt(),Spine.GetCompactPoseIndex(Bones).GetInt(),UpperSpine[0].GetCompactPoseIndex(Bones).GetInt(),UpperSpine[1].GetCompactPoseIndex(Bones).GetInt()};
	for (int32 I=0; I<5; ++I) { Rig.Spine[I]=ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(SpineIndices[I]));Rig.Spine[I].AddToTranslation(BaseShift); }
	for (int32 I=0; I<4; ++I)
	{
		const FVector A=ReferenceCS.GetComponentSpaceTransform(Limb[I][0].GetCompactPoseIndex(Bones)).GetLocation();
		const FVector B=ReferenceCS.GetComponentSpaceTransform(Limb[I][1].GetCompactPoseIndex(Bones)).GetLocation();
		const FVector C=ReferenceCS.GetComponentSpaceTransform(Limb[I][2].GetCompactPoseIndex(Bones)).GetLocation();
		Rig.Reach[I]=FVector::Distance(A,B)+FVector::Distance(B,C);Rig.Goals[I]=Goals[I];
		if (I<2) Rig.Thigh[I]=A+BaseShift;
		else { Rig.Shoulder[I-2]=ReferenceCS.GetComponentSpaceTransform(Limb[I][0].GetCompactPoseIndex(Bones));Rig.Shoulder[I-2].AddToTranslation(BaseShift); }
	}
	FSWCannonSpineLimits Limits;
	Limits.ForwardShift=MaxPelvisForwardShift; Limits.SideShift=MaxPelvisSideShift; Limits.Crouch=MaxReachCrouch;
	Limits.MaxBend=MaxWaistBend; Limits.MaxSide=MaxSideLean; Limits.MaxYaw=MaxTorsoYaw; Limits.YawShare=TorsoYawFollow;
	Limits.RestBend=HipLean+ForwardLean+ExtraWaistBend;Limits.ShoulderAboveGrip=ShoulderAboveGrip;
	if (!bReachInitialized) { ReachSolution=FSWCannonSpineState();ReachSolution.Bend=Limits.RestBend;ReachApplied=ReachSolution;bReachInitialized=true; }
	if (bCachedMounted && LastReachSampleTime!=SampleTime)
	{
		ReachSolution=SWCannonSpine::Solve(Rig,Limits,ReachSolution);
		const float T=FMath::Clamp(SampleDeltaTime*FMath::Max(1.f,BlendSpeed),0.f,1.f);
		ReachApplied.Offset=FMath::Lerp(ReachApplied.Offset,ReachSolution.Offset,T);
		ReachApplied.Bend=FMath::Lerp(ReachApplied.Bend,ReachSolution.Bend,T);
		ReachApplied.Side=FMath::Lerp(ReachApplied.Side,ReachSolution.Side,T);
		ReachApplied.Yaw=FMath::Lerp(ReachApplied.Yaw,ReachSolution.Yaw,T);
		LastReachSampleTime=SampleTime;
	}
	const FSWCannonSpinePose ReachPose=SWCannonSpine::Build(Rig,ReachApplied);
	SetCS(PelvisIndex,ReachPose.Hip);
	for (int32 I=0; I<5; ++I) SetCS(SpineIndices[I],ReachPose.Spine[I]);
	for (int32 I=2; I<4; ++I)
		JointPoles[I]=(ReferenceCS.GetComponentSpaceTransform(Limb[I][1].GetCompactPoseIndex(Bones)).GetRelativeTransform(ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(SpineIndices[4])))*ReachPose.Spine[4]).GetLocation();
	const int32 HeadIndex=Head.GetCompactPoseIndex(Bones).GetInt();FTransform Look=CS[HeadIndex];
	const FQuat RefHead=ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(HeadIndex)).GetRotation();
	const FQuat BoreRotation=FQuat(Up,FMath::DegreesToRadians(HeadYaw))*FQuat(Right,FMath::DegreesToRadians(HeadPitch));
	const FQuat BodyRotation=(Look.GetRotation()*RefHead.Inverse()).GetNormalized();
	const FVector ToGrip=(Targets[2]+Targets[3])*.5f-Look.GetLocation();
	FSWCannonHeadLimits HeadLimits;
	HeadLimits.Yaw=MaxHeadYaw; HeadLimits.PitchUp=MaxHeadPitchUp; HeadLimits.PitchDown=MaxHeadPitchDown;
	HeadLimits.Cone=MaxHeadAimCorrection; HeadLimits.FollowSpeed=HeadAimFollowSpeed;
	HeadLimits.DegreesPerSecond=HeadAimDegreesPerSecond;
	const FQuat DesiredCorrection=SWCannonHeadAim::Target(BodyRotation,BoreRotation.RotateVector(Forward),
		ToGrip,GripLookWeight,Forward,Right,Up,HeadLimits);
	if (!bHeadAimInitialized)
	{
		HeadCorrection=FQuat::Identity; LastHeadAimSampleTime=-1.; bHeadAimInitialized=true;
	}
	if (bCachedMounted && LastHeadAimSampleTime!=SampleTime)
	{
		HeadCorrection=SWCannonHeadAim::Follow(HeadCorrection,DesiredCorrection,SampleDeltaTime,Forward,Right,Up,HeadLimits);
		LastHeadAimSampleTime=SampleTime;
	}
	Look.SetRotation((BodyRotation*HeadCorrection*RefHead).GetNormalized());
	SetCS(HeadIndex,Look);
	for (int32 I=0; I<4; ++I)
	{
		const int32 A = Limb[I][0].GetCompactPoseIndex(Bones).GetInt();
		const int32 B = Limb[I][1].GetCompactPoseIndex(Bones).GetInt();
		const int32 C = Limb[I][2].GetCompactPoseIndex(Bones).GetInt();
		FTransform Upper=CS[A], Lower=CS[B], End=CS[C];
		const float Reach = FVector::Distance(Upper.GetLocation(),Lower.GetLocation()) + FVector::Distance(Lower.GetLocation(),End.GetLocation());
		const FVector Goal = Upper.GetLocation() + (Goals[I]-Upper.GetLocation()).GetClampedToMaxSize(Reach*.98f);
		AnimationCore::SolveTwoBoneIK(Upper,Lower,End,JointPoles[I],Goal,false,1.,1.);
		if (I>=2) End.SetRotation(HandRotations[I-2]);
		else End.SetRotation(ReferenceCS.GetComponentSpaceTransform(Limb[I][2].GetCompactPoseIndex(Bones)).GetRotation());
		SetCS(A,Upper); SetCS(B,Lower); SetCS(C,End);
	}
	// Curl fingers from their own reference axes rather than assuming male/female bone Euler axes.
	for (int32 Side=0; Side<2; ++Side) for (int32 Finger=0; Finger<5; ++Finger) for (int32 Joint=0; Joint<3; ++Joint)
	{
		const int32 Index=Fingers[Side][Finger][Joint].GetCompactPoseIndex(Bones).GetInt();
		const FTransform Ref=ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(Index));
		const FTransform RefHand=ReferenceCS.GetComponentSpaceTransform(Limb[Side+2][2].GetCompactPoseIndex(Bones));
		const int32 Start=Fingers[Side][Finger][0].GetCompactPoseIndex(Bones).GetInt();
		const int32 Next=Fingers[Side][Finger][1].GetCompactPoseIndex(Bones).GetInt();
		const FVector Along=RefHand.InverseTransformVectorNoScale(ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(Next)).GetLocation()-ReferenceCS.GetComponentSpaceTransform(FCompactPoseBoneIndex(Start)).GetLocation()).GetSafeNormal();
		const FVector AxisHand=FVector::CrossProduct(PalmBack[Side],Along).GetSafeNormal();
		const FVector AxisLocal=Ref.InverseTransformVectorNoScale(RefHand.TransformVectorNoScale(AxisHand));
		FTransform FingerPose=CS[Index];
		const float Bend=FingerCurl*(Finger==0 ? .4f : Joint==0 ? .65f : 1.f);
		FingerPose.SetRotation((FingerPose.GetRotation()*FQuat(AxisLocal,FMath::DegreesToRadians(Bend))).GetNormalized());
		SetCS(Index,FingerPose);
	}
	FQuat MountedEndRotation[4];
	for (int32 I=0; I<4; ++I) MountedEndRotation[I]=CS[Limb[I][2].GetCompactPoseIndex(Bones).GetInt()].GetRotation();
	// Blend local transforms to retain bone lengths. Do not use that FK blend
	// as the final limb result: blend the hand/foot endpoints in component space.
	for (int32 I=0; I<Count; ++I) Local[I]=Parents[I]>=0 ? CS[I].GetRelativeTransform(CS[Parents[I]]) : CS[I];
	for (int32 I=0; I<Count; ++I)
	{
		const FTransform MountedLocal=Local[I];
		// FCSPose lazily stores converted component-space values in its compact
		// pose. Derive locals from the cached CS transforms, not GetPose()[I].
		const FTransform InputLocal=Parents[I]>=0 ? InputCS[I].GetRelativeTransform(InputCS[Parents[I]]) : InputCS[I];
		Local[I].Blend(InputLocal,MountedLocal,MountWeight);
		CS[I]=Parents[I]>=0 ? Local[I]*CS[Parents[I]] : Local[I];
	}
	// Possession can switch the incoming locomotion pose during this blend.
	// Follow from the previous output instead of exposing that discontinuity.
	// This filter exists only during release, fades out with the operator layer,
	// and is bypassed entirely for movement input, montages, jumps and swimming.
	const bool bStabilizeRelease=!bCachedMounted && PreviousOutputLocal.Num()==Count;
	const bool bNewSample=SampleTime!=LastEvaluatedSampleTime;
	const float Follow=bNewSample ? FMath::Lerp(1.f,FMath::Min(.12f,1.f-FMath::Exp(-FMath::Max(1.f,BlendOutSpeed)*SampleDeltaTime)),FMath::Clamp(MountWeight/.2f,0.f,1.f)) : 0.f;
	const double DesiredHeadStep=bStabilizeRelease ? FVector::Distance(CS[HeadIndex].GetLocation(),PreviousOutputCS[HeadIndex].GetLocation()) : 0.;
	if (bStabilizeRelease) for (int32 I=0; I<Count; ++I)
	{
		Local[I].Blend(PreviousOutputLocal[I],Local[I],Follow);
		CS[I]=Parents[I]>=0 ? Local[I]*CS[Parents[I]] : Local[I];
	}
	for (int32 I=0; I<4; ++I)
	{
		const int32 A=Limb[I][0].GetCompactPoseIndex(Bones).GetInt();
		const int32 B=Limb[I][1].GetCompactPoseIndex(Bones).GetInt();
		const int32 C=Limb[I][2].GetCompactPoseIndex(Bones).GetInt();
		FTransform Upper=CS[A],Lower=CS[B],End=CS[C];
		const float Reach=FVector::Distance(Upper.GetLocation(),Lower.GetLocation())+FVector::Distance(Lower.GetLocation(),End.GetLocation());
		FVector Desired=FMath::Lerp(InputCS[C].GetLocation(),Goals[I],MountWeight);
		if (bStabilizeRelease) Desired=FMath::Lerp(PreviousOutputCS[C].GetLocation(),Desired,Follow);
		const FVector Goal=Upper.GetLocation()+(Desired-Upper.GetLocation()).GetClampedToMaxSize(Reach*FMath::Lerp(1.f,.98f,MountWeight));
		FVector Pole=FMath::Lerp(InputCS[B].GetLocation(),JointPoles[I],MountWeight);
		if (bStabilizeRelease) Pole=FMath::Lerp(PreviousOutputCS[B].GetLocation(),Pole,Follow);
		AnimationCore::SolveTwoBoneIK(Upper,Lower,End,Pole,Goal,false,1.,1.);
		FQuat EndRotation=FQuat::Slerp(InputCS[C].GetRotation(),MountedEndRotation[I],MountWeight).GetNormalized();
		if (bStabilizeRelease) EndRotation=FQuat::Slerp(PreviousOutputCS[C].GetRotation(),EndRotation,Follow).GetNormalized();
		End.SetRotation(EndRotation);
		SetCS(A,Upper); SetCS(B,Lower); SetCS(C,End);
	}
#if WITH_EDITOR
	if (bStabilizeRelease && bNewSample && DesiredHeadStep>15.)
		UE_LOG(LogTemp,Display,TEXT("[CannonRelease] Weight=%.4f Dt=%.4f BaseHeadStep=%.2f DesiredHeadStep=%.2f FilteredHeadStep=%.2f"),MountWeight,SampleDeltaTime,
			PreviousInputCS.Num()==Count ? FVector::Distance(InputCS[HeadIndex].GetLocation(),PreviousInputCS[HeadIndex].GetLocation()) : 0.,DesiredHeadStep,
			FVector::Distance(CS[HeadIndex].GetLocation(),PreviousOutputCS[HeadIndex].GetLocation()));
#endif
	PreviousOutputLocal=Local; PreviousOutputCS=CS; PreviousInputCS=InputCS; LastEvaluatedSampleTime=SampleTime;
	for (int32 I=0; I<Count; ++I) Out.Emplace(FCompactPoseBoneIndex(I),CS[I]);
}
