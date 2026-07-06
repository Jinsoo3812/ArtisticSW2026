#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Animation/SWTrajectoryComponent.h"
#include "BaseGameplayTags.h"
#include "CharacterTrajectoryComponent.h"
#include "ChooserFunctionLibrary.h"
#include "Chooser.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#ifdef UE_LOG
#undef UE_LOG
#endif
#define UE_LOG(...)

static TAutoConsoleVariable<int32> CVarMotionMatchingDebugLogging(
    TEXT("p.MMDebugging"),
    1,
    TEXT("Motion Matching diagnostics. 0: Disabled, 1: Transition/search events, 2: Verbose frame/node/stack dumps"),
    ECVF_Default
);

DEFINE_LOG_CATEGORY_STATIC(LogMotionMatchingCapture, Log, All);

namespace
{
    constexpr bool bSearchFallOffEveryUpdate = false;
    constexpr float FallOffSearchThrottleTime = 0.12f;
    constexpr float FallOffActiveSearchDuration = 0.45f;
    constexpr float SuppressedSearchThrottleTime = 3600.0f;
    constexpr float RemoteTransitionAllowedTimeSlack = 0.06f;
    constexpr float RemoteTransitionMaxForwardJump = 0.20f;
    constexpr float BlendStackDuplicateTimeSlack = 0.08f;
    constexpr float RemoteLandingHistoryGroundLockWindow = 0.15f;

    bool IsTransitionMotionMatchingState(ELocomotionState State)
    {
        return State == ELocomotionState::Start ||
            State == ELocomotionState::Stop ||
            State == ELocomotionState::Landing;
    }

    bool IsTransitionAnimationForState(const UAnimationAsset* AnimationAsset, ELocomotionState State)
    {
        const FString AssetName = GetNameSafe(AnimationAsset);
        switch (State)
        {
        case ELocomotionState::Start:
            return AssetName.Contains(TEXT("_Start"));
        case ELocomotionState::Stop:
            return AssetName.Contains(TEXT("_Stop"));
        case ELocomotionState::Landing:
            return AssetName.Contains(TEXT("_Land"));
        default:
            return false;
        }
    }

    bool IsAnyTransitionAnimation(const UObject* AnimationAsset)
    {
        const FString AssetName = GetNameSafe(AnimationAsset);
        return AssetName.Contains(TEXT("_Start")) ||
            AssetName.Contains(TEXT("_Stop")) ||
            AssetName.Contains(TEXT("_Land"));
    }

    bool IsLocomotionLoopAnimation(const UObject* AnimationAsset)
    {
        const FString AssetName = GetNameSafe(AnimationAsset);
        return AssetName.Contains(TEXT("_Run_Loop")) ||
            AssetName.Contains(TEXT("_Walk_Loop")) ||
            AssetName.Contains(TEXT("_Idle"));
    }

    float ClampAnimationAssetTime(const UAnimationAsset* AnimationAsset, float Time)
    {
        if (!AnimationAsset)
        {
            return FMath::Max(0.f, Time);
        }

        const float PlayLength = AnimationAsset->GetPlayLength();
        if (PlayLength <= UE_SMALL_NUMBER)
        {
            return FMath::Max(0.f, Time);
        }

        return FMath::Clamp(Time, 0.f, FMath::Max(0.f, PlayLength - KINDA_SMALL_NUMBER));
    }

    bool RestoreBlendStackTopPlayer(
        const FAnimationUpdateContext& Context,
        FAnimNode_MotionMatching& MotionMatchingNode,
        UAnimationAsset* AnimationAsset,
        float AccumulatedTime)
    {
        if (MotionMatchingNode.AnimPlayers.IsEmpty() || !AnimationAsset)
        {
            return false;
        }

        FBlendStackAnimPlayer& TopPlayer = MotionMatchingNode.AnimPlayers[0];
        FAnimationInitializeContext InitContext(Context.AnimInstanceProxy, Context.SharedContext);
        TopPlayer.Initialize(
            InitContext,
            AnimationAsset,
            ClampAnimationAssetTime(AnimationAsset, AccumulatedTime),
            TopPlayer.IsLooping(),
            TopPlayer.GetMirror(),
            nullptr,
            0.f,
            nullptr,
            TopPlayer.GetBlendOption(),
            TopPlayer.GetBlendParameters(),
            TopPlayer.GetPlayRate(),
            0.f,
            TopPlayer.GetPoseLinkIndex(),
            NAME_None,
            EAnimGroupRole::CanBeLeader,
            EAnimSyncMethod::DoNotSync,
            false);
        return true;
    }

    bool RemoveLowerTransitionPlayersForState(FAnimNode_MotionMatching& MotionMatchingNode, ELocomotionState State)
    {
        if (MotionMatchingNode.AnimPlayers.Num() <= 1)
        {
            return false;
        }

        bool bRemoved = false;
        for (int32 PlayerIndex = MotionMatchingNode.AnimPlayers.Num() - 1; PlayerIndex >= 1; --PlayerIndex)
        {
            const UAnimationAsset* PlayerAsset = MotionMatchingNode.AnimPlayers[PlayerIndex].GetAnimationAsset();
            if (IsTransitionAnimationForState(PlayerAsset, State))
            {
                MotionMatchingNode.AnimPlayers.RemoveAt(PlayerIndex, 1, EAllowShrinking::No);
                bRemoved = true;
            }
        }

        return bRemoved;
    }

    bool StabilizeRemoteTransitionBlendStackPlayer(
        const FAnimationUpdateContext& Context,
        FAnimNode_MotionMatching& MotionMatchingNode,
        FCachedMotionMatchingNodeInfo& NodeInfo,
        ELocomotionState State)
    {
        NodeInfo.bPostUpdateCollapsedTransitionStack = false;

        if (MotionMatchingNode.AnimPlayers.IsEmpty())
        {
            NodeInfo.bHasRemoteTransitionLock = false;
            NodeInfo.LockedRemoteTransitionAnim.Reset();
            NodeInfo.LockedRemoteTransitionTime = 0.f;
            return false;
        }

        FBlendStackAnimPlayer& TopPlayer = MotionMatchingNode.AnimPlayers[0];
        UAnimationAsset* TopAsset = TopPlayer.GetAnimationAsset();
        if (!IsTransitionAnimationForState(TopAsset, State))
        {
            NodeInfo.bHasRemoteTransitionLock = false;
            NodeInfo.LockedRemoteTransitionAnim.Reset();
            NodeInfo.LockedRemoteTransitionTime = 0.f;
            return false;
        }

        NodeInfo.bPostUpdateCollapsedTransitionStack =
            RemoveLowerTransitionPlayersForState(MotionMatchingNode, State);

        UAnimationAsset* LockedAsset = NodeInfo.LockedRemoteTransitionAnim.Get();
        const bool bNeedsNewLock =
            !NodeInfo.bHasRemoteTransitionLock ||
            NodeInfo.LockedRemoteTransitionState != State ||
            !IsTransitionAnimationForState(LockedAsset, State);

        if (bNeedsNewLock)
        {
            UAnimationAsset* SeedAsset = TopAsset;
            float SeedTime = TopPlayer.GetAccumulatedTime();

            UAnimationAsset* PreUpdateAsset = const_cast<UAnimationAsset*>(
                Cast<const UAnimationAsset>(NodeInfo.PreUpdateStackTopAnim.Get()));
            if (IsTransitionAnimationForState(PreUpdateAsset, State) &&
                NodeInfo.PreUpdateStackTopTime > 0.f &&
                (PreUpdateAsset != TopAsset || TopPlayer.GetAccumulatedTime() > NodeInfo.PreUpdateStackTopTime + RemoteTransitionMaxForwardJump))
            {
                SeedAsset = PreUpdateAsset;
                SeedTime = NodeInfo.PreUpdateStackTopTime + FMath::Max(0.f, Context.GetDeltaTime());
            }

            NodeInfo.LockedRemoteTransitionAnim = SeedAsset;
            NodeInfo.LockedRemoteTransitionTime = ClampAnimationAssetTime(SeedAsset, SeedTime);
            NodeInfo.LockedRemoteTransitionState = State;
            NodeInfo.bHasRemoteTransitionLock = true;
            LockedAsset = SeedAsset;
        }

        const FPoseSearchBlueprintResult& CurrentResult = MotionMatchingNode.GetMotionMatchingState().SearchResult;
        if (State == ELocomotionState::Landing && CurrentResult.SelectedAnim.Get() == LockedAsset)
        {
            const float SelectedTime = ClampAnimationAssetTime(LockedAsset, CurrentResult.SelectedTime);
            if (SelectedTime > NodeInfo.LockedRemoteTransitionTime + RemoteTransitionAllowedTimeSlack)
            {
                NodeInfo.LockedRemoteTransitionTime = SelectedTime;
            }
        }

        const float PlayRate = FMath::Max(0.f, TopPlayer.GetPlayRate());
        const float ExpectedTime = ClampAnimationAssetTime(
            LockedAsset,
            NodeInfo.LockedRemoteTransitionTime + FMath::Max(0.f, Context.GetDeltaTime()) * PlayRate);
        const float TopTime = TopPlayer.GetAccumulatedTime();
        const bool bAssetChanged = TopAsset != LockedAsset;
        const bool bTimeRewound = TopTime + RemoteTransitionAllowedTimeSlack < ExpectedTime;
        const bool bTimeJumpedForward = TopTime > ExpectedTime + RemoteTransitionMaxForwardJump;

        if (bAssetChanged || bTimeRewound || bTimeJumpedForward)
        {
            if (RestoreBlendStackTopPlayer(Context, MotionMatchingNode, LockedAsset, ExpectedTime))
            {
                NodeInfo.LockedRemoteTransitionTime = ExpectedTime;
                NodeInfo.bPostUpdateCollapsedTransitionStack =
                    RemoveLowerTransitionPlayersForState(MotionMatchingNode, State) ||
                    NodeInfo.bPostUpdateCollapsedTransitionStack;
                return true;
            }
        }

        NodeInfo.LockedRemoteTransitionTime = ClampAnimationAssetTime(
            LockedAsset,
            FMath::Max(NodeInfo.LockedRemoteTransitionTime, TopTime));
        return false;
    }

    void ClearMotionMatchingCaptureLog()
    {
        const FString LogFilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MMCapture.log"));
        const FString SummaryLogFilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MMCaptureSummary.log"));
        IFileManager::Get().Delete(*LogFilePath, false, true);
        IFileManager::Get().Delete(*SummaryLogFilePath, false, true);
    }

    void OnMotionMatchingDebugLoggingChanged(IConsoleVariable* Variable)
    {
        if (Variable && Variable->GetInt() > 0)
        {
            ClearMotionMatchingCaptureLog();
        }
    }

    struct FMotionMatchingDebugLoggingResetRegistration
    {
        FMotionMatchingDebugLoggingResetRegistration()
        {
            CVarMotionMatchingDebugLogging.AsVariable()->SetOnChangedCallback(
                FConsoleVariableDelegate::CreateStatic(&OnMotionMatchingDebugLoggingChanged));
        }
    };

    static FMotionMatchingDebugLoggingResetRegistration MotionMatchingDebugLoggingResetRegistration;

    const TCHAR* FormatNetRole(ENetRole Role)
    {
        switch (Role)
        {
        case ROLE_None:
            return TEXT("None");
        case ROLE_SimulatedProxy:
            return TEXT("SimProxy");
        case ROLE_AutonomousProxy:
            return TEXT("AutoProxy");
        case ROLE_Authority:
            return TEXT("Authority");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* FormatNetMode(ENetMode NetMode)
    {
        switch (NetMode)
        {
        case NM_Standalone:
            return TEXT("Standalone");
        case NM_DedicatedServer:
            return TEXT("DedicatedServer");
        case NM_ListenServer:
            return TEXT("ListenServer");
        case NM_Client:
            return TEXT("Client");
        default:
            return TEXT("Unknown");
        }
    }

    const FTransformTrajectorySample* FindClosestTrajectorySample(const FTransformTrajectory& Trajectory, float TargetTime)
    {
        const FTransformTrajectorySample* Closest = nullptr;
        float ClosestDifference = MAX_flt;
        for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
        {
            const float Difference = FMath::Abs(Sample.TimeInSeconds - TargetTime);
            if (Difference < ClosestDifference)
            {
                ClosestDifference = Difference;
                Closest = &Sample;
            }
        }

        return Closest;
    }

    FString FormatLocalDirectionLabel(const FVector2D& Direction)
    {
        if (Direction.IsNearlyZero(0.05f))
        {
            return TEXT("None");
        }

        const FVector2D Normalized = Direction.GetSafeNormal();
        const float AbsRight = FMath::Abs(Normalized.X);
        const float AbsForward = FMath::Abs(Normalized.Y);
        if (AbsForward >= AbsRight * 1.35f)
        {
            return Normalized.Y >= 0.f ? TEXT("F") : TEXT("B");
        }
        if (AbsRight >= AbsForward * 1.35f)
        {
            return Normalized.X >= 0.f ? TEXT("R") : TEXT("L");
        }

        return FString::Printf(TEXT("%s%s"),
            Normalized.Y >= 0.f ? TEXT("F") : TEXT("B"),
            Normalized.X >= 0.f ? TEXT("R") : TEXT("L"));
    }

    const TCHAR* ResolveLandAssetDirection(const UObject* Asset)
    {
        const FString AssetName = GetNameSafe(Asset);
        if (AssetName.Contains(TEXT("_Jump_LL_Land")) || AssetName.Contains(TEXT("_LL_Land")))
        {
            return TEXT("LL");
        }
        if (AssetName.Contains(TEXT("_Jump_RL_Land")) || AssetName.Contains(TEXT("_RL_Land")))
        {
            return TEXT("RL");
        }
        if (AssetName.Contains(TEXT("_Jump_B_Land")) || AssetName.Contains(TEXT("_B_Land")))
        {
            return TEXT("B");
        }
        if (AssetName.Contains(TEXT("_Jump_F_Land")) || AssetName.Contains(TEXT("_F_Land")))
        {
            return TEXT("F");
        }
        return TEXT("Unknown");
    }

    FVector GetTrajectorySampleRelativeLocation(const FTransformTrajectory& Trajectory, const FTransform& ReferenceTransform, float TargetTime, bool& bOutFound)
    {
        bOutFound = false;
        const FTransformTrajectorySample* Closest = FindClosestTrajectorySample(Trajectory, TargetTime);
        if (!Closest)
        {
            return FVector::ZeroVector;
        }

        bOutFound = true;
        const FTransform Transform = Closest->GetTransform();
        const FTransform RelativeTransform = Transform.GetRelativeTransform(ReferenceTransform);
        return RelativeTransform.GetLocation();
    }

    FString FormatTrajectoryDirectionProbe(const FTransformTrajectory& Trajectory, const FTransform& ReferenceTransform, float TargetTime)
    {
        bool bFound = false;
        const FVector RelativeLocation = GetTrajectorySampleRelativeLocation(Trajectory, ReferenceTransform, TargetTime, bFound);
        if (!bFound)
        {
            return FString::Printf(TEXT("T%.1f=NA"), TargetTime);
        }

        return FString::Printf(TEXT("T%.1f=(RelX=%.1f,RelY=%.1f,Label=%s)"),
            TargetTime,
            RelativeLocation.X,
            RelativeLocation.Y,
            *FormatLocalDirectionLabel(FVector2D(RelativeLocation.X, RelativeLocation.Y)));
    }

    FString FormatTrajectorySample(const FTransformTrajectory& Trajectory, const FTransform& ReferenceTransform, float TargetTime)
    {
        const FTransformTrajectorySample* Closest = FindClosestTrajectorySample(Trajectory, TargetTime);
        if (!Closest)
        {
            return FString::Printf(TEXT("T%.1f=NA"), TargetTime);
        }

        const FTransform Transform = Closest->GetTransform();
        const FTransform RelativeTransform = Transform.GetRelativeTransform(ReferenceTransform);
        const FVector Location = RelativeTransform.GetLocation();
        return FString::Printf(TEXT("T%.1f=(RelX=%.1f,RelY=%.1f,RelZ=%.1f|Yaw=%.1f)"),
            TargetTime,
            Location.X,
            Location.Y,
            Location.Z,
            Transform.Rotator().Yaw);
    }

    FString FormatLandingTrajectorySamples(const FTransformTrajectory& Trajectory, const FTransform& ReferenceTransform)
    {
        return FString::Printf(TEXT("%s %s %s %s %s %s %s"),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, -0.30f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, -0.10f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, 0.00f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, 0.10f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, 0.30f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, 0.60f),
            *FormatTrajectorySample(Trajectory, ReferenceTransform, 1.00f));
    }

    const TCHAR* ResolveFallOffAssetDirection(const UObject* Asset)
    {
        const FString AssetName = GetNameSafe(Asset);
        if (AssetName.Contains(TEXT("_Jump_LL_Off")))
        {
            return TEXT("LL");
        }
        if (AssetName.Contains(TEXT("_Jump_RL_Off")))
        {
            return TEXT("RL");
        }
        if (AssetName.Contains(TEXT("_Jump_B_Off")))
        {
            return TEXT("B");
        }
        if (AssetName.Contains(TEXT("_Jump_F_Off")))
        {
            return TEXT("F");
        }
        return TEXT("Unknown");
    }

    FString FormatMotionMatchingBlendStack(const FAnimNode_MotionMatching& MotionMatchingNode)
    {
        if (MotionMatchingNode.AnimPlayers.IsEmpty())
        {
            return TEXT("Empty");
        }

        FString BlendStack;
        for (int32 Index = 0; Index < MotionMatchingNode.AnimPlayers.Num(); ++Index)
        {
            const FBlendStackAnimPlayer& AnimPlayer = MotionMatchingNode.AnimPlayers[Index];
            if (!BlendStack.IsEmpty())
            {
                BlendStack += TEXT(" | ");
            }

            BlendStack += FString::Printf(
                TEXT("#%d:%s[time=%.3f,rate=%.2f,weight=%.2f,in=%.2f/%.2f,active=%d]"),
                Index,
                *GetNameSafe(AnimPlayer.GetAnimationAsset()),
                AnimPlayer.GetAccumulatedTime(),
                AnimPlayer.GetPlayRate(),
                AnimPlayer.GetBlendInWeight(),
                AnimPlayer.GetCurrentBlendInTime(),
                AnimPlayer.GetTotalBlendInTime(),
                AnimPlayer.IsActive() ? 1 : 0);
        }

        return BlendStack;
    }

    const FBlendStackAnimPlayer* GetBlendStackTopPlayer(const FAnimNode_MotionMatching& MotionMatchingNode)
    {
        return MotionMatchingNode.AnimPlayers.IsEmpty() ? nullptr : &MotionMatchingNode.AnimPlayers[0];
    }

    FString FormatCompactBlendStackHead(const FAnimNode_MotionMatching& MotionMatchingNode)
    {
        const FBlendStackAnimPlayer* TopPlayer = GetBlendStackTopPlayer(MotionMatchingNode);
        if (!TopPlayer)
        {
            return TEXT("Empty");
        }

        FString StackHead = FString::Printf(
            TEXT("#0=%s@%.3f/w%.2f/in%.2f"),
            *GetNameSafe(TopPlayer->GetAnimationAsset()),
            TopPlayer->GetAccumulatedTime(),
            TopPlayer->GetBlendInWeight(),
            TopPlayer->GetCurrentBlendInTime());

        if (MotionMatchingNode.AnimPlayers.Num() > 1)
        {
            const FBlendStackAnimPlayer& PreviousPlayer = MotionMatchingNode.AnimPlayers[1];
            StackHead += FString::Printf(
                TEXT(" #1=%s@%.3f/w%.2f/in%.2f"),
                *GetNameSafe(PreviousPlayer.GetAnimationAsset()),
                PreviousPlayer.GetAccumulatedTime(),
                PreviousPlayer.GetBlendInWeight(),
                PreviousPlayer.GetCurrentBlendInTime());
        }

        return StackHead;
    }

    const TCHAR* FormatBoolChange(bool bBefore, bool bAfter)
    {
        if (bBefore == bAfter)
        {
            return bAfter ? TEXT("1") : TEXT("0");
        }
        return bAfter ? TEXT("0->1") : TEXT("1->0");
    }

    FString FormatPoseSearchInterruptModeValue(int64 Value)
    {
        const UEnum* InterruptEnum = StaticEnum<EPoseSearchInterruptMode>();
        return InterruptEnum ? InterruptEnum->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
    }

    FString ReadPoseSearchInterruptMode(const FCachedMotionMatchingNodeInfo& Info, const FAnimNode_MotionMatching& MotionMatchingNode)
    {
        if (!Info.NextUpdateInterruptModeProperty)
        {
            return TEXT("NA");
        }

        const void* ValuePtr = Info.NextUpdateInterruptModeProperty->ContainerPtrToValuePtr<void>(&MotionMatchingNode);
        if (!ValuePtr)
        {
            return TEXT("NA");
        }

        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Info.NextUpdateInterruptModeProperty))
        {
            const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
            return FormatPoseSearchInterruptModeValue(Value);
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Info.NextUpdateInterruptModeProperty))
        {
            return FormatPoseSearchInterruptModeValue(ByteProperty->GetPropertyValue(ValuePtr));
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Info.NextUpdateInterruptModeProperty))
        {
            return FormatPoseSearchInterruptModeValue(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
        }

        return TEXT("Unknown");
    }

    float GetFallOffLateralTrajectoryLeadTime(const ABasePlayer& CharacterOwner, const FVector& CharacterVelocity)
    {
        const FVector LocalVelocity = CharacterOwner.GetActorTransform().InverseTransformVectorNoScale(CharacterVelocity);
        const float ForwardSpeed = FMath::Abs(LocalVelocity.X);
        const float LateralSpeed = FMath::Abs(LocalVelocity.Y);
        const float PlanarSpeed = ForwardSpeed + LateralSpeed;
        if (PlanarSpeed <= UE_KINDA_SMALL_NUMBER)
        {
            return 0.f;
        }

        const float LateralRatio = LateralSpeed / PlanarSpeed;
        return FMath::GetMappedRangeValueClamped(FVector2D(0.35f, 0.85f), FVector2D(0.f, 0.24f), LateralRatio);
    }

    void ApplyFallingPredictionToTrajectory(FTransformTrajectory& Trajectory, const ABasePlayer& CharacterOwner)
    {
        if (Trajectory.Samples.IsEmpty())
        {
            return;
        }

        const UCharacterMovementComponent* MovementComponent = CharacterOwner.GetCharacterMovement();
        if (!MovementComponent || !MovementComponent->IsFalling())
        {
            return;
        }

        const FVector CharacterVelocity = MovementComponent->Velocity;
        const float GravityZ = MovementComponent->GetGravityZ();
        if (GravityZ >= -UE_KINDA_SMALL_NUMBER)
        {
            return;
        }

        const float LateralLeadTime = GetFallOffLateralTrajectoryLeadTime(CharacterOwner, CharacterVelocity);
        for (FTransformTrajectorySample& Sample : Trajectory.Samples)
        {
            const float SampleTime = Sample.TimeInSeconds;
            if (SampleTime <= UE_KINDA_SMALL_NUMBER)
            {
                continue;
            }

            const float PredictedTime = SampleTime + LateralLeadTime;
            FTransform SampleTransform = Sample.GetTransform();
            FVector SampleLocation = SampleTransform.GetLocation();
            SampleLocation.Z += CharacterVelocity.Z * PredictedTime + 0.5f * GravityZ * PredictedTime * PredictedTime;
            SampleTransform.SetLocation(SampleLocation);
            Sample.SetTransform(SampleTransform);
        }
    }

    void AppendMotionMatchingAnimCaptureLine(const FString& Line)
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

    void AppendMotionMatchingSummaryCaptureLine(const FString& Line)
    {
        const FString LogFilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MMCaptureSummary.log"));
        const FString StampedLine = FString::Printf(
            TEXT("[%s] %s%s"),
            *FDateTime::Now().ToString(TEXT("%H:%M:%S.%s")),
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

FMotionMatchingAnimInstanceProxy::FMotionMatchingAnimInstanceProxy()
    : FAnimInstanceProxy()
{
}

FMotionMatchingAnimInstanceProxy::FMotionMatchingAnimInstanceProxy(UAnimInstance* InAnimInstance)
    : FAnimInstanceProxy(InAnimInstance)
{
}

void FMotionMatchingAnimInstanceProxy::CacheNodes(UAnimInstance* InAnimInstance)
{
    if (!InAnimInstance) return;

    CachedMMNodes.Empty();
    CachedHistoryNodes.Empty();

    UClass* AnimClass = InAnimInstance->GetClass();
    for (TFieldIterator<FProperty> PropIt(AnimClass); PropIt; ++PropIt)
    {
        FStructProperty* StructProp = CastField<FStructProperty>(*PropIt);
        if (!StructProp || !StructProp->Struct) continue;

        if (StructProp->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))
        {
            FCachedMotionMatchingNodeInfo Info;
            Info.NodeProperty = StructProp;

            if (FProperty* DbProp = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("Database")))
            {
                Info.DatabaseProperty = CastField<FObjectProperty>(DbProp);
            }
            if (FProperty* SearchThrottleProp = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("SearchThrottleTime")))
            {
                Info.SearchThrottleTimeProperty = CastField<FFloatProperty>(SearchThrottleProp);
            }
            if (FProperty* ShouldSearchProp = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("bShouldSearch")))
            {
                Info.ShouldSearchProperty = CastField<FBoolProperty>(ShouldSearchProp);
            }
            if (FProperty* InterruptModeProp = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("NextUpdateInterruptMode")))
            {
                Info.NextUpdateInterruptModeProperty = InterruptModeProp;
            }

            CachedMMNodes.Add(Info);
        }
        else if (StructProp->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector::StaticStruct()))
        {
            FCachedHistoryCollectorNodeInfo Info;
            Info.NodeProperty = StructProp;

            TArray<FName> HistoryPropNames = { FName("TransformTrajectory"), FName("Trajectory") };
            for (const FName& PropName : HistoryPropNames)
            {
                if (FProperty* HistoryProp = StructProp->Struct->FindPropertyByName(PropName))
                {
                    if (FStructProperty* HistoryStructProp = CastField<FStructProperty>(HistoryProp))
                    {
                        if (HistoryStructProp->Struct == FTransformTrajectory::StaticStruct())
                        {
                            Info.TrajectoryProperty = HistoryStructProp;
                            break;
                        }
                    }
                }
            }

            CachedHistoryNodes.Add(Info);
        }
    }

    bNodesCached = true;
}

void FMotionMatchingAnimInstanceProxy::UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InGroupRelevancyName)
{
    UAnimInstance* AnimInstanceObj = Cast<UAnimInstance>(GetAnimInstanceObject());
    if (!AnimInstanceObj)
    {
        FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InGroupRelevancyName);
        return;
    }

    if (!bNodesCached)
    {
        CacheNodes(AnimInstanceObj);
    }

    // The trajectory and database/search policy must be applied before the graph update.
    // Applying them afterwards allows one stale ground-locomotion search/blend to enter
    // the stack on the first FallOff frame.
    for (const FCachedHistoryCollectorNodeInfo& Info : CachedHistoryNodes)
    {
        if (Info.NodeProperty && Info.TrajectoryProperty)
        {
            FAnimNode_PoseSearchHistoryCollector* HistoryNode = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(AnimInstanceObj);
            if (HistoryNode)
            {
                void* HistoryPropPtr = Info.TrajectoryProperty->ContainerPtrToValuePtr<void>(HistoryNode);
                if (HistoryPropPtr)
                {
                    Info.TrajectoryProperty->Struct->CopyScriptStruct(HistoryPropPtr, &ThreadSafeData.MovementData.Trajectory);
                }
            }
        }
    }

    DebugLogAccumulator += InContext.GetDeltaTime();
    const bool bCaptureFrame =
        AnimInstanceObj && AnimInstanceObj->GetWorld() && AnimInstanceObj->GetWorld()->IsGameWorld() &&
        CVarMotionMatchingDebugLogging.GetValueOnAnyThread() > 0 &&
        DebugLogAccumulator >= 0.05f;
    if (bCaptureFrame)
    {
        DebugLogAccumulator = 0.f;
    }
    const bool bIsAirLoopDebugPhase =
        ThreadSafeData.AirData.bIsInAir &&
        !ThreadSafeData.AirData.bIsJumping &&
        !ThreadSafeData.AirData.bIsFallOffStart &&
        !ThreadSafeData.LandingData.bIsLanding &&
        !ThreadSafeData.LandingData.bLandingRequested;
    const bool bCaptureMotionMatchingFrame = bCaptureFrame;
    const APawn* OwnerPawn = AnimInstanceObj ? AnimInstanceObj->TryGetPawnOwner() : nullptr;
    const bool bIsRemoteSimProxy = OwnerPawn && OwnerPawn->GetLocalRole() == ROLE_SimulatedProxy;

    int32 MotionMatchingNodeIndex = 0;
    for (FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
    {
        if (Info.NodeProperty && Info.DatabaseProperty)
        {
            FAnimNode_MotionMatching* MMNode = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
            if (MMNode)
            {
                const bool bIsFallOffStartPhase =
                    ThreadSafeData.AirData.bIsInAir &&
                    !ThreadSafeData.AirData.bIsJumping &&
                    ThreadSafeData.AirData.bIsFallOffStart;

                if (!Info.bDefaultSearchThrottleCached && Info.SearchThrottleTimeProperty)
                {
                    Info.DefaultSearchThrottleTime = Info.SearchThrottleTimeProperty->GetPropertyValue_InContainer(MMNode);
                    Info.bDefaultSearchThrottleCached = true;
                }

                if (!Info.bDefaultMaxActiveBlendsCached)
                {
                    Info.DefaultMaxActiveBlends = MMNode->GetMaxActiveBlends();
                    Info.bDefaultMaxActiveBlendsCached = true;
                }

                const FMotionMatchingState& PreUpdateMotionMatchingState = MMNode->GetMotionMatchingState();
                const FPoseSearchBlueprintResult& PreUpdateResult = PreUpdateMotionMatchingState.SearchResult;
                const FBlendStackAnimPlayer* PreUpdateTopPlayer = GetBlendStackTopPlayer(*MMNode);

                Info.PreUpdateSelectedAnim = PreUpdateResult.SelectedAnim.Get();
                Info.PreUpdateSelectedDatabase = PreUpdateResult.SelectedDatabase.Get();
                Info.PreUpdateSelectedTime = PreUpdateResult.SelectedTime;
                Info.bPreUpdateContinue = PreUpdateResult.bIsContinuingPoseSearch;
                Info.PreUpdateStackTopAnim = PreUpdateTopPlayer ? PreUpdateTopPlayer->GetAnimationAsset() : nullptr;
                Info.PreUpdateStackTopTime = PreUpdateTopPlayer ? PreUpdateTopPlayer->GetAccumulatedTime() : 0.f;
                Info.PreUpdateStackNum = MMNode->AnimPlayers.Num();

                const UPoseSearchDatabase* SelectedDatabaseBeforeUpdate =
                    MMNode->GetMotionMatchingState().SearchResult.SelectedDatabase.Get();
                const bool bSearchResultDatabaseChanged = SelectedDatabaseBeforeUpdate != CurrentActivePoseSearchDatabase.Get();
                const bool bAppliedDatabaseChanged = Info.AppliedDatabase != CurrentActivePoseSearchDatabase;
                Info.bPreUpdateDbChanged = bSearchResultDatabaseChanged;
                Info.bPreUpdateAppliedDbChanged = bAppliedDatabaseChanged;
                const ELocomotionState CurrentMotionState = static_cast<ELocomotionState>(ThreadSafeData.GroundData.GroundMotionMode);
                const bool bIsTransitionState = IsTransitionMotionMatchingState(CurrentMotionState);
                const bool bIsRemoteIdleHold =
                    bIsRemoteSimProxy &&
                    CurrentMotionState == ELocomotionState::Idle &&
                    !ThreadSafeData.InputData.bHasMoveInput &&
                    !bSearchResultDatabaseChanged &&
                    !bAppliedDatabaseChanged;
                if (bAppliedDatabaseChanged)
                {
                    const bool bInvalidateContinuingPose =
                        (!bIsTransitionState && CurrentMotionState != ELocomotionState::InAir);
                    const EPoseSearchInterruptMode InterruptMode = bInvalidateContinuingPose
                        ? EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose
                        : EPoseSearchInterruptMode::InterruptOnDatabaseChange;

                    if (CurrentActivePoseSearchDatabase)
                    {
                        MMNode->SetDatabaseToSearch(
                            CurrentActivePoseSearchDatabase,
                            InterruptMode);
                    }
                    else
                    {
                        MMNode->ResetDatabasesToSearch(
                            InterruptMode);
                    }
                    Info.AppliedDatabase = CurrentActivePoseSearchDatabase;
                }
                else if (bIsTransitionState)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }
                else if (bIsFallOffStartPhase && !bSearchResultDatabaseChanged && !bAppliedDatabaseChanged)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }
                else if (bIsRemoteIdleHold)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }

                const bool bIsAirLoopPhase =
                    ThreadSafeData.AirData.bIsInAir &&
                    !ThreadSafeData.AirData.bIsJumping &&
                    !ThreadSafeData.AirData.bIsFallOffStart &&
                    !ThreadSafeData.LandingData.bIsLanding &&
                    !ThreadSafeData.LandingData.bLandingRequested;

                if (bIsFallOffStartPhase || bIsAirLoopPhase)
                {
                    MMNode->SetMaxActiveBlends(1);
                }
                else
                {
                    MMNode->SetMaxActiveBlends(Info.DefaultMaxActiveBlends);
                }
                Info.PreUpdateMaxActiveBlends = MMNode->GetMaxActiveBlends();

                if (Info.SearchThrottleTimeProperty)
                {
                    float SearchThrottleTime = Info.DefaultSearchThrottleTime;
                    if (bIsFallOffStartPhase && !bSearchResultDatabaseChanged)
                    {
                        if (!bSearchFallOffEveryUpdate || ThreadSafeData.MovementData.FallOffElapsedTime >= FallOffActiveSearchDuration)
                        {
                            SearchThrottleTime = SuppressedSearchThrottleTime;
                        }
                        else
                        {
                            SearchThrottleTime = FMath::Max(Info.DefaultSearchThrottleTime, FallOffSearchThrottleTime);
                        }
                    }
                    else if (bIsAirLoopPhase)
                    {
                        const bool bRemoteStableAirLoop =
                            bIsRemoteSimProxy &&
                            !bSearchResultDatabaseChanged &&
                            !bAppliedDatabaseChanged;
                        SearchThrottleTime = bRemoteStableAirLoop
                            ? SuppressedSearchThrottleTime
                            : Info.DefaultSearchThrottleTime;
                    }
                    else
                    {
                        UMotionMatchingAnimInstance* MMAnim = Cast<UMotionMatchingAnimInstance>(AnimInstanceObj);
                        const ULocomotionAnimStateComponent* StateComp = MMAnim ? MMAnim->CachedLocomotionStateComponent.Get() : nullptr;
                        if (StateComp && (StateComp->CurrentState == ELocomotionState::Start || StateComp->CurrentState == ELocomotionState::Stop || StateComp->CurrentState == ELocomotionState::Landing))
                        {
                            // Start 및 Stop 상태에서는 최초 진입 프레임(bAppliedDatabaseChanged) 및 에셋 교체 직후 프레임(bSearchResultDatabaseChanged)에만 검색을 허용하고,
                            // 그 외의 프레임에서는 추가 평가(재검색)를 차단하여 재생 중인 에셋이 중간에 끊기거나 오매칭되는 현상을 방지합니다.
                            if (!bSearchResultDatabaseChanged && !bAppliedDatabaseChanged)
                            {
                                SearchThrottleTime = SuppressedSearchThrottleTime;
                            }
                        }
                        else if (bIsRemoteIdleHold)
                        {
                            SearchThrottleTime = SuppressedSearchThrottleTime;
                        }
                    }
                    Info.SearchThrottleTimeProperty->SetPropertyValue_InContainer(MMNode, SearchThrottleTime);
                    Info.PreUpdateThrottle = SearchThrottleTime;

                    if (Info.ShouldSearchProperty)
                    {
                        const bool bShouldSearch = (SearchThrottleTime < SuppressedSearchThrottleTime - 1.f);
                        Info.ShouldSearchProperty->SetPropertyValue_InContainer(MMNode, bShouldSearch);
                        Info.bPreUpdateShouldSearch = bShouldSearch;
                    }
                    else
                    {
                        Info.bPreUpdateShouldSearch = true;
                    }
                }
                else
                {
                    Info.PreUpdateThrottle = -1.f;
                    Info.bPreUpdateShouldSearch = true;
                }

            }
        }
        ++MotionMatchingNodeIndex;
    }

    FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InGroupRelevancyName);

    const ELocomotionState UpdatedMotionState = static_cast<ELocomotionState>(ThreadSafeData.GroundData.GroundMotionMode);
    const APawn* UpdatedPawn = AnimInstanceObj ? AnimInstanceObj->TryGetPawnOwner() : nullptr;
    const bool bStabilizeRemoteTransition =
        UpdatedPawn &&
        UpdatedPawn->GetLocalRole() == ROLE_SimulatedProxy &&
        UpdatedMotionState == ELocomotionState::Landing;
    if (bStabilizeRemoteTransition)
    {
        for (FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
        {
            Info.bPostUpdateRestoredTransitionStack = false;
            Info.bPostUpdateCollapsedTransitionStack = false;
            if (Info.NodeProperty)
            {
                FAnimNode_MotionMatching* MMNode =
                    Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
                if (MMNode)
                {
                    Info.bPostUpdateRestoredTransitionStack =
                        StabilizeRemoteTransitionBlendStackPlayer(InContext, *MMNode, Info, UpdatedMotionState);
                }
            }
        }
    }
    else
    {
        for (FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
        {
            Info.bPostUpdateRestoredTransitionStack = false;
            Info.bPostUpdateCollapsedTransitionStack = false;
            Info.bHasRemoteTransitionLock = false;
            Info.LockedRemoteTransitionAnim.Reset();
            Info.LockedRemoteTransitionTime = 0.f;
        }
    }

    const int32 DebugLevel = CVarMotionMatchingDebugLogging.GetValueOnAnyThread();
    if (DebugLevel > 0 && AnimInstanceObj && AnimInstanceObj->GetWorld() && AnimInstanceObj->GetWorld()->IsGameWorld())
    {
        const APawn* DebugPawn = AnimInstanceObj->TryGetPawnOwner();
        const AActor* DebugActor = DebugPawn ? Cast<const AActor>(DebugPawn) : nullptr;
        const FString DebugPawnName = DebugActor ? DebugActor->GetName() : GetNameSafe(AnimInstanceObj);
        const ENetRole DebugRole = DebugActor ? DebugActor->GetLocalRole() : ROLE_None;
        const ENetMode DebugNetMode = DebugActor ? DebugActor->GetNetMode() : NM_Standalone;
        const FString NodeStateName = StaticEnum<ELocomotionState>()->GetNameStringByValue(
            static_cast<int64>(ThreadSafeData.GroundData.GroundMotionMode));
        const bool bIsTransitionState = IsTransitionMotionMatchingState(UpdatedMotionState);

        MotionMatchingNodeIndex = 0;
        for (FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
        {
            if (Info.NodeProperty)
            {
                const FAnimNode_MotionMatching* MMNode =
                    Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
                if (MMNode)
                {
                    const FMotionMatchingState& MotionMatchingState = MMNode->GetMotionMatchingState();
                    const FPoseSearchBlueprintResult& Result = MotionMatchingState.SearchResult;
                    const FBlendStackAnimPlayer* PostTopPlayer = GetBlendStackTopPlayer(*MMNode);
                    const UObject* PostTopAnim = PostTopPlayer ? PostTopPlayer->GetAnimationAsset() : nullptr;
                    const float PostTopTime = PostTopPlayer ? PostTopPlayer->GetAccumulatedTime() : 0.f;
                    const int32 PostStackNum = MMNode->AnimPlayers.Num();
                    const FBlendStackAnimPlayer* PostPreviousPlayer = PostStackNum > 1 ? &MMNode->AnimPlayers[1] : nullptr;
                    const UObject* PostPreviousAnim = PostPreviousPlayer ? PostPreviousPlayer->GetAnimationAsset() : nullptr;
                    const float PostPreviousTime = PostPreviousPlayer ? PostPreviousPlayer->GetAccumulatedTime() : 0.f;

                    const bool bTopChanged = Info.PreUpdateStackTopAnim.Get() != PostTopAnim;
                    const bool bTopRewound = Info.PreUpdateStackTopAnim.Get() == PostTopAnim &&
                        Info.PreUpdateStackTopTime > 0.08f &&
                        PostTopTime + 0.04f < Info.PreUpdateStackTopTime;
                    const bool bStackGrew = PostStackNum > Info.PreUpdateStackNum;
                    const bool bSelectedChanged = Info.PreUpdateSelectedAnim.Get() != Result.SelectedAnim.Get();
                    const bool bSelectedRewound = Info.PreUpdateSelectedAnim.Get() == Result.SelectedAnim.Get() &&
                        Info.PreUpdateSelectedTime > 0.08f &&
                        Result.SelectedTime + 0.04f < Info.PreUpdateSelectedTime;
                    const bool bChangedSinceLastLog = Info.LastStackTopAnim.Get() != PostTopAnim ||
                        FMath::Abs(Info.LastStackTopTime - PostTopTime) > 0.20f ||
                        Info.LastStackNum != PostStackNum;
                    const bool bShouldLogTransitionFrame = bIsTransitionState && (bCaptureFrame || bTopChanged || bTopRewound || bStackGrew || bSelectedChanged || bSelectedRewound);
                    const bool bShouldLogStackEvent = bStackGrew || bTopChanged || bTopRewound || bSelectedChanged || bSelectedRewound || Info.bPostUpdateRestoredTransitionStack || Info.bPostUpdateCollapsedTransitionStack;
                    const bool bDuplicateTop = PostTopAnim && PostPreviousAnim == PostTopAnim &&
                        FMath::Abs(PostPreviousTime - PostTopTime) <= BlendStackDuplicateTimeSlack;
                    const bool bLoopOverTransition = IsLocomotionLoopAnimation(PostTopAnim) && IsAnyTransitionAnimation(PostPreviousAnim);
                    const bool bTransitionOverLoop = IsAnyTransitionAnimation(PostTopAnim) && IsLocomotionLoopAnimation(PostPreviousAnim);
                    const bool bTransitionEnter = bIsTransitionState && IsAnyTransitionAnimation(PostTopAnim) &&
                        !IsAnyTransitionAnimation(Info.PreUpdateStackTopAnim.Get());
                    const bool bTransitionExit = !bIsTransitionState &&
                        IsAnyTransitionAnimation(Info.PreUpdateStackTopAnim.Get()) &&
                        IsLocomotionLoopAnimation(PostTopAnim);
                    const bool bLandingQueryProbe =
                        UpdatedMotionState == ELocomotionState::Landing &&
                        (bTransitionEnter ||
                            Info.bPreUpdateAppliedDbChanged ||
                            Info.bPreUpdateDbChanged ||
                            bSelectedChanged ||
                            bSelectedRewound ||
                            bTopChanged ||
                            bTopRewound);

                    if (bShouldLogTransitionFrame || bShouldLogStackEvent || (DebugLevel >= 2 && bChangedSinceLastLog))
                    {
                        const FString InterruptModeName = ReadPoseSearchInterruptMode(Info, *MMNode);
                        const FString MotionMatchingEventLine = FString::Printf(
                            TEXT("[MMCAP_MM] Pawn=%s Net=%s Role=%s Anim=%s Node=%d State=%s Transition=%d RequestedPSD=%s AppliedPSD=%s PreSel=%s@%.3f PostSel=%s@%.3f Continue=%s Cost=%.3f PreTop=%s@%.3f PostTop=%s@%.3f Stack=%d->%d Grew=%d TopChanged=%d TopRewind=%d SelChanged=%d SelRewind=%d DbChanged=%d AppliedDbChanged=%d Throttle=%.3f ShouldSearch=%d MaxBlend=%d Interrupt=%s Restored=%d Collapsed=%d DupTop=%d LoopOverTransition=%d TransitionOverLoop=%d Lock=%s@%.3f Head={%s}"),
                            *DebugPawnName,
                            FormatNetMode(DebugNetMode),
                            FormatNetRole(DebugRole),
                            *AnimInstanceObj->GetName(),
                            MotionMatchingNodeIndex,
                            *NodeStateName,
                            bIsTransitionState ? 1 : 0,
                            *GetNameSafe(CurrentActivePoseSearchDatabase),
                            *GetNameSafe(Result.SelectedDatabase),
                            *GetNameSafe(Info.PreUpdateSelectedAnim.Get()),
                            Info.PreUpdateSelectedTime,
                            *GetNameSafe(Result.SelectedAnim),
                            Result.SelectedTime,
                            FormatBoolChange(Info.bPreUpdateContinue, Result.bIsContinuingPoseSearch),
                            Result.SearchCost,
                            *GetNameSafe(Info.PreUpdateStackTopAnim.Get()),
                            Info.PreUpdateStackTopTime,
                            *GetNameSafe(PostTopAnim),
                            PostTopTime,
                            Info.PreUpdateStackNum,
                            PostStackNum,
                            bStackGrew ? 1 : 0,
                            bTopChanged ? 1 : 0,
                            bTopRewound ? 1 : 0,
                            bSelectedChanged ? 1 : 0,
                            bSelectedRewound ? 1 : 0,
                            Info.bPreUpdateDbChanged ? 1 : 0,
                            Info.bPreUpdateAppliedDbChanged ? 1 : 0,
                            Info.PreUpdateThrottle,
                            Info.bPreUpdateShouldSearch ? 1 : 0,
                            Info.PreUpdateMaxActiveBlends,
                            *InterruptModeName,
                            Info.bPostUpdateRestoredTransitionStack ? 1 : 0,
                            Info.bPostUpdateCollapsedTransitionStack ? 1 : 0,
                            bDuplicateTop ? 1 : 0,
                            bLoopOverTransition ? 1 : 0,
                            bTransitionOverLoop ? 1 : 0,
                            *GetNameSafe(Info.LockedRemoteTransitionAnim.Get()),
                            Info.LockedRemoteTransitionTime,
                            *FormatCompactBlendStackHead(*MMNode));
                        UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *MotionMatchingEventLine);
                        AppendMotionMatchingAnimCaptureLine(MotionMatchingEventLine);

                        if (bLandingQueryProbe)
                        {
                            const FTransform OwnerComponentTransform = AnimInstanceObj->GetOwningComponent()
                                ? AnimInstanceObj->GetOwningComponent()->GetComponentTransform()
                                : FTransform::Identity;
                            const FString LandQueryLine = FString::Printf(
                                TEXT("[MMCAP_LAND_QUERY] Pawn=%s Net=%s Role=%s Anim=%s Node=%d Event=%s RequestedPSD=%s AppliedPSD=%s PreDB=%s PreSel=%s@%.3f PostSel=%s@%.3f Continue=%s Cost=%.3f PreTop=%s@%.3f PostTop=%s@%.3f Stack=%d->%d Interrupt=%s Throttle=%.3f ShouldSearch=%d Input=(R=%.2f,F=%.2f,H=%d) Move=(Vel=%.1f,%.1f,%.1f Local=R%.1f,F%.1f,Z%.1f Accel=%.1f,%.1f,%.1f) Air=(In=%d,Jump=%d,FallOff=%d) Land=(Req=%d,Heavy=%d,Moving=%d,Sprint=%d,Ground=%.1f,StartGround=%.1f,Fall=%.1f,Dir=R%.2f,F%.2f,Time=%.3f,PhysAir=%d) RawTraj={%s} FinalTraj={%s} Head={%s}"),
                                *DebugPawnName,
                                FormatNetMode(DebugNetMode),
                                FormatNetRole(DebugRole),
                                *AnimInstanceObj->GetName(),
                                MotionMatchingNodeIndex,
                                bTransitionEnter ? TEXT("ENTER") : (Info.bPreUpdateAppliedDbChanged ? TEXT("DB_APPLY") : TEXT("SEARCH")),
                                *GetNameSafe(CurrentActivePoseSearchDatabase),
                                *GetNameSafe(Result.SelectedDatabase),
                                *GetNameSafe(Info.PreUpdateSelectedDatabase.Get()),
                                *GetNameSafe(Info.PreUpdateSelectedAnim.Get()),
                                Info.PreUpdateSelectedTime,
                                *GetNameSafe(Result.SelectedAnim),
                                Result.SelectedTime,
                                FormatBoolChange(Info.bPreUpdateContinue, Result.bIsContinuingPoseSearch),
                                Result.SearchCost,
                                *GetNameSafe(Info.PreUpdateStackTopAnim.Get()),
                                Info.PreUpdateStackTopTime,
                                *GetNameSafe(PostTopAnim),
                                PostTopTime,
                                Info.PreUpdateStackNum,
                                PostStackNum,
                                *InterruptModeName,
                                Info.PreUpdateThrottle,
                                Info.bPreUpdateShouldSearch ? 1 : 0,
                                ThreadSafeData.InputData.MoveInput.X,
                                ThreadSafeData.InputData.MoveInput.Y,
                                ThreadSafeData.InputData.bHasMoveInput ? 1 : 0,
                                ThreadSafeData.MovementData.Velocity.X,
                                ThreadSafeData.MovementData.Velocity.Y,
                                ThreadSafeData.MovementData.Velocity.Z,
                                ThreadSafeData.MovementData.VelocityLocal.Y,
                                ThreadSafeData.MovementData.VelocityLocal.X,
                                ThreadSafeData.MovementData.VelocityLocal.Z,
                                ThreadSafeData.MovementData.Acceleration.X,
                                ThreadSafeData.MovementData.Acceleration.Y,
                                ThreadSafeData.MovementData.Acceleration.Z,
                                ThreadSafeData.AirData.bIsInAir ? 1 : 0,
                                ThreadSafeData.AirData.bIsJumping ? 1 : 0,
                                ThreadSafeData.AirData.bIsFallOffStart ? 1 : 0,
                                ThreadSafeData.LandingData.bLandingRequested ? 1 : 0,
                                ThreadSafeData.LandingData.bUseHeavyLand ? 1 : 0,
                                ThreadSafeData.LandingData.bLandWasMoving ? 1 : 0,
                                ThreadSafeData.LandingData.bLandWasSprinting ? 1 : 0,
                                ThreadSafeData.LandingData.GroundSpeed,
                                ThreadSafeData.LandingData.LandStartGroundSpeed,
                                ThreadSafeData.LandingData.LandStartFallSpeed,
                                ThreadSafeData.LandingData.LandMoveDirection.X,
                                ThreadSafeData.LandingData.LandMoveDirection.Y,
                                ThreadSafeData.LandingData.LandingElapsedTime,
                                ThreadSafeData.LandingData.bIsPhysicallyInAir ? 1 : 0,
                                *FormatLandingTrajectorySamples(ThreadSafeData.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform),
                                *FormatLandingTrajectorySamples(ThreadSafeData.MovementData.Trajectory, OwnerComponentTransform),
                                *FormatCompactBlendStackHead(*MMNode));
                            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *LandQueryLine);
                            AppendMotionMatchingAnimCaptureLine(LandQueryLine);
                            AppendMotionMatchingSummaryCaptureLine(LandQueryLine);

                            const FRotator ActorRotation = DebugActor ? DebugActor->GetActorRotation() : FRotator::ZeroRotator;
                            const FRotator MeshRotation = OwnerComponentTransform.Rotator();
                            const FVector2D LandDirection = ThreadSafeData.LandingData.LandMoveDirection.GetSafeNormal();
                            const FVector2D InputDirection = ThreadSafeData.InputData.MoveInput.GetSafeNormal();
                            const FString LandDirectionLine = FString::Printf(
                                TEXT("[MMCAP_LAND_DIR] Pawn=%s Role=%s Node=%d Event=%s LandDir=(R=%.2f,F=%.2f,%s) InputDir=(R=%.2f,F=%.2f,%s,H=%d) PreAsset=%s Dir=%s@%.3f PostAsset=%s Dir=%s@%.3f PreTop=%s Dir=%s@%.3f PostTop=%s Dir=%s@%.3f RawTrajDir={%s %s} FinalTrajDir={%s %s} Yaw=(Actor=%.1f,Mesh=%.1f) Cost=%.3f PSD=%s"),
                                *DebugPawnName,
                                FormatNetRole(DebugRole),
                                MotionMatchingNodeIndex,
                                bTransitionEnter ? TEXT("ENTER") : (Info.bPreUpdateAppliedDbChanged ? TEXT("DB_APPLY") : TEXT("SEARCH")),
                                LandDirection.X,
                                LandDirection.Y,
                                *FormatLocalDirectionLabel(LandDirection),
                                InputDirection.X,
                                InputDirection.Y,
                                *FormatLocalDirectionLabel(InputDirection),
                                ThreadSafeData.InputData.bHasMoveInput ? 1 : 0,
                                *GetNameSafe(Info.PreUpdateSelectedAnim.Get()),
                                ResolveLandAssetDirection(Info.PreUpdateSelectedAnim.Get()),
                                Info.PreUpdateSelectedTime,
                                *GetNameSafe(Result.SelectedAnim),
                                ResolveLandAssetDirection(Result.SelectedAnim),
                                Result.SelectedTime,
                                *GetNameSafe(Info.PreUpdateStackTopAnim.Get()),
                                ResolveLandAssetDirection(Info.PreUpdateStackTopAnim.Get()),
                                Info.PreUpdateStackTopTime,
                                *GetNameSafe(PostTopAnim),
                                ResolveLandAssetDirection(PostTopAnim),
                                PostTopTime,
                                *FormatTrajectoryDirectionProbe(ThreadSafeData.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform, 0.30f),
                                *FormatTrajectoryDirectionProbe(ThreadSafeData.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform, 1.00f),
                                *FormatTrajectoryDirectionProbe(ThreadSafeData.MovementData.Trajectory, OwnerComponentTransform, 0.30f),
                                *FormatTrajectoryDirectionProbe(ThreadSafeData.MovementData.Trajectory, OwnerComponentTransform, 1.00f),
                                ActorRotation.Yaw,
                                MeshRotation.Yaw,
                                Result.SearchCost,
                                *GetNameSafe(Result.SelectedDatabase));
                            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *LandDirectionLine);
                            AppendMotionMatchingAnimCaptureLine(LandDirectionLine);
                            AppendMotionMatchingSummaryCaptureLine(LandDirectionLine);
                        }

                        TArray<FString> SummaryEvents;
                        if (bTransitionEnter)
                        {
                            SummaryEvents.Add(TEXT("ENTER"));
                        }
                        if (bTransitionExit)
                        {
                            SummaryEvents.Add(TEXT("EXIT"));
                        }
                        if (bStackGrew)
                        {
                            SummaryEvents.Add(TEXT("STACK_GROW"));
                        }
                        if (bDuplicateTop)
                        {
                            SummaryEvents.Add(TEXT("DUP"));
                        }
                        if (bTopRewound || bSelectedRewound)
                        {
                            SummaryEvents.Add(TEXT("REWIND"));
                        }
                        if (bLoopOverTransition)
                        {
                            SummaryEvents.Add(TEXT("LOOP_OVER_TRANSITION"));
                        }
                        if (bTransitionOverLoop)
                        {
                            SummaryEvents.Add(TEXT("TRANSITION_OVER_LOOP"));
                        }
                        if (Info.bPostUpdateRestoredTransitionStack)
                        {
                            SummaryEvents.Add(TEXT("RESTORE"));
                        }
                        if (Info.bPostUpdateCollapsedTransitionStack)
                        {
                            SummaryEvents.Add(TEXT("COLLAPSE"));
                        }
                        if (SummaryEvents.IsEmpty() && bIsTransitionState && bCaptureFrame)
                        {
                            SummaryEvents.Add(TEXT("TRACE"));
                        }

                        if (!SummaryEvents.IsEmpty())
                        {
                            const FString SummaryLine = FString::Printf(
                                TEXT("[MMSUM] Pawn=%s Role=%s Node=%d State=%s Event=%s PSD=%s Applied=%s PreTop=%s@%.3f PostTop=%s@%.3f Sel=%s@%.3f Stack=%d->%d MaxBlend=%d Interrupt=%s Throttle=%.3f Should=%d Restore=%d Collapse=%d Lock=%s@%.3f Input=(R=%.2f,F=%.2f,H=%d) Air=(In=%d,Jump=%d,FallOff=%d) Land=(Landing=%d,Req=%d,Heavy=%d,Speed=%.1f,StartGround=%.1f,Fall=%.1f,Dir=(R=%.2f,F=%.2f),Time=%.3f) Head={%s}"),
                                *DebugPawnName,
                                FormatNetRole(DebugRole),
                                static_cast<int32>(MotionMatchingNodeIndex),
                                *NodeStateName,
                                *FString::Join(SummaryEvents, TEXT("+")),
                                *GetNameSafe(CurrentActivePoseSearchDatabase),
                                *GetNameSafe(Result.SelectedDatabase),
                                *GetNameSafe(Info.PreUpdateStackTopAnim.Get()),
                                Info.PreUpdateStackTopTime,
                                *GetNameSafe(PostTopAnim),
                                PostTopTime,
                                *GetNameSafe(Result.SelectedAnim),
                                Result.SelectedTime,
                                static_cast<int32>(Info.PreUpdateStackNum),
                                static_cast<int32>(PostStackNum),
                                static_cast<int32>(Info.PreUpdateMaxActiveBlends),
                                *InterruptModeName,
                                Info.PreUpdateThrottle,
                                Info.bPreUpdateShouldSearch ? int32(1) : int32(0),
                                Info.bPostUpdateRestoredTransitionStack ? int32(1) : int32(0),
                                Info.bPostUpdateCollapsedTransitionStack ? int32(1) : int32(0),
                                *GetNameSafe(Info.LockedRemoteTransitionAnim.Get()),
                                Info.LockedRemoteTransitionTime,
                                ThreadSafeData.InputData.MoveInput.X,
                                ThreadSafeData.InputData.MoveInput.Y,
                                ThreadSafeData.InputData.bHasMoveInput ? int32(1) : int32(0),
                                ThreadSafeData.AirData.bIsInAir ? int32(1) : int32(0),
                                ThreadSafeData.AirData.bIsJumping ? int32(1) : int32(0),
                                ThreadSafeData.AirData.bIsFallOffStart ? int32(1) : int32(0),
                                ThreadSafeData.LandingData.bIsLanding ? int32(1) : int32(0),
                                ThreadSafeData.LandingData.bLandingRequested ? int32(1) : int32(0),
                                ThreadSafeData.LandingData.bUseHeavyLand ? int32(1) : int32(0),
                                ThreadSafeData.LandingData.GroundSpeed,
                                ThreadSafeData.LandingData.LandStartGroundSpeed,
                                ThreadSafeData.LandingData.LandStartFallSpeed,
                                ThreadSafeData.LandingData.LandMoveDirection.X,
                                ThreadSafeData.LandingData.LandMoveDirection.Y,
                                ThreadSafeData.LandingData.LandingElapsedTime,
                                *FormatCompactBlendStackHead(*MMNode));
                            AppendMotionMatchingSummaryCaptureLine(SummaryLine);
                        }
                    }

                    Info.LastStackTopAnim = PostTopAnim;
                    Info.LastStackTopTime = PostTopTime;
                    Info.LastStackNum = PostStackNum;
                }
            }
            ++MotionMatchingNodeIndex;
        }
    }

    if (bCaptureMotionMatchingFrame && CVarMotionMatchingDebugLogging.GetValueOnAnyThread() >= 2)
    {
        const APawn* DebugPawn = AnimInstanceObj ? AnimInstanceObj->TryGetPawnOwner() : nullptr;
        const AActor* DebugActor = DebugPawn ? Cast<const AActor>(DebugPawn) : nullptr;
        const FString DebugPawnName = DebugActor ? DebugActor->GetName() : GetNameSafe(AnimInstanceObj);
        const ENetRole DebugRole = DebugActor ? DebugActor->GetLocalRole() : ROLE_None;
        const ENetMode DebugNetMode = DebugActor ? DebugActor->GetNetMode() : NM_Standalone;

        MotionMatchingNodeIndex = 0;
        for (FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
        {
            if (Info.NodeProperty)
            {
                const FAnimNode_MotionMatching* MMNode =
                    Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
                if (MMNode)
                {
                    const FMotionMatchingState& MotionMatchingState = MMNode->GetMotionMatchingState();
                    const FPoseSearchBlueprintResult& Result = MotionMatchingState.SearchResult;

                    const float CurrentThrottle = Info.SearchThrottleTimeProperty
                        ? Info.SearchThrottleTimeProperty->GetPropertyValue_InContainer(MMNode)
                        : -1.f;
                    const bool bIsFallOffStartPhase =
                        ThreadSafeData.AirData.bIsInAir &&
                        !ThreadSafeData.AirData.bIsJumping &&
                        ThreadSafeData.AirData.bIsFallOffStart;
                    const bool bDatabaseChanged = Result.SelectedDatabase.Get() != CurrentActivePoseSearchDatabase.Get();
                    const FString PreviousSelection = FString::Printf(
                        TEXT("%s@%.3f"),
                        *GetNameSafe(Info.LastSelectedAnim.Get()),
                        Info.LastSelectedTime);
                    const FString NodeStateName = StaticEnum<ELocomotionState>()->GetNameStringByValue(
                        static_cast<int64>(ThreadSafeData.GroundData.GroundMotionMode));
                    const FString NodeDebugLine = FString::Printf(
                        TEXT("[MMCAP_NODE] Pawn=%s Net=%s Role=%s Anim=%s Node=%d Property=%s State=%s Input=(R=%.2f,F=%.2f) HasInput=%d RequestedPSD=%s SelectedDB=%s SelectedAsset=%s AssetDir=%s Time=%.3f Cost=%.3f Continue=%d Prev=%s Mirrored=%d Loop=%d PlayRate=%.3f Blend=(%.2f,%.2f,%.2f) Throttle=%.3f FallOff=%d ShouldSearch=%d DBChanged=%d StackNum=%d"),
                        *DebugPawnName,
                        FormatNetMode(DebugNetMode),
                        FormatNetRole(DebugRole),
                        *AnimInstanceObj->GetName(),
                        MotionMatchingNodeIndex,
                        *Info.NodeProperty->GetName(),
                        *NodeStateName,
                        ThreadSafeData.InputData.MoveInput.X,
                        ThreadSafeData.InputData.MoveInput.Y,
                        ThreadSafeData.InputData.bHasMoveInput ? 1 : 0,
                        *GetNameSafe(CurrentActivePoseSearchDatabase),
                        *GetNameSafe(Result.SelectedDatabase),
                        *GetNameSafe(Result.SelectedAnim),
                        ResolveFallOffAssetDirection(Result.SelectedAnim),
                        Result.SelectedTime,
                        Result.SearchCost,
                        Result.bIsContinuingPoseSearch ? 1 : 0,
                        *PreviousSelection,
                        Result.bIsMirrored ? 1 : 0,
                        Result.bLoop ? 1 : 0,
                        Result.WantedPlayRate,
                        Result.BlendParameters.X,
                        Result.BlendParameters.Y,
                        Result.BlendParameters.Z,
                        CurrentThrottle,
                        bIsFallOffStartPhase ? 1 : 0,
                        Info.ShouldSearchProperty ? (Info.ShouldSearchProperty->GetPropertyValue_InContainer(MMNode) ? 1 : 0) : 1,
                        bDatabaseChanged ? 1 : 0,
                        MMNode->AnimPlayers.Num());
                    UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *NodeDebugLine);
                    AppendMotionMatchingAnimCaptureLine(NodeDebugLine);

                    const FString BlendStackDebugLine = FString::Printf(
                        TEXT("[MMCAP_BLENDSTACK] Pawn=%s Net=%s Role=%s Anim=%s Node=%d FallOff=%d Stack=%s"),
                        *DebugPawnName,
                        FormatNetMode(DebugNetMode),
                        FormatNetRole(DebugRole),
                        *AnimInstanceObj->GetName(),
                        MotionMatchingNodeIndex,
                        bIsFallOffStartPhase ? 1 : 0,
                        *FormatMotionMatchingBlendStack(*MMNode));
                    UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *BlendStackDebugLine);
                    AppendMotionMatchingAnimCaptureLine(BlendStackDebugLine);

                    Info.LastSelectedAnim = Result.SelectedAnim.Get();
                    Info.LastSelectedTime = Result.SelectedTime;
                }
            }
            ++MotionMatchingNodeIndex;
        }
    }

    if (bCaptureMotionMatchingFrame && CVarMotionMatchingDebugLogging.GetValueOnAnyThread() >= 2)
    {
        const FAnimThreadSafeData& Data = ThreadSafeData;
        const FString StateDebugLine = FString::Printf(
            TEXT("[MMCAP_STATE] Anim=%s State=%s IsInAir=%d IsJumping=%d IsFallOff=%d FallOffElapsed=%.3f Input=(R=%.2f,F=%.2f) HasInput=%d Vel=(%.1f,%.1f,%.1f) VelLocal=(R=%.1f,F=%.1f,Z=%.1f) Accel=(%.1f,%.1f,%.1f) Ground=%.1f Vertical=%.1f PhysAir=%d Landing=%d Requested=%d Heavy=%d Moving=%d SprintLand=%d LandSpeed=%.1f FallSpeed=%.1f LandDir=(R=%.2f,F=%.2f) LandTime=%.3f"),
            *AnimInstanceObj->GetName(),
            *StaticEnum<ELocomotionState>()->GetNameStringByValue(Data.GroundData.GroundMotionMode),
            Data.AirData.bIsInAir ? 1 : 0,
            Data.AirData.bIsJumping ? 1 : 0,
            Data.AirData.bIsFallOffStart ? 1 : 0,
            Data.MovementData.FallOffElapsedTime,
            Data.InputData.MoveInput.X,
            Data.InputData.MoveInput.Y,
            Data.InputData.bHasMoveInput ? 1 : 0,
            Data.MovementData.Velocity.X,
            Data.MovementData.Velocity.Y,
            Data.MovementData.Velocity.Z,
            Data.MovementData.VelocityLocal.Y,
            Data.MovementData.VelocityLocal.X,
            Data.MovementData.VelocityLocal.Z,
            Data.MovementData.Acceleration.X,
            Data.MovementData.Acceleration.Y,
            Data.MovementData.Acceleration.Z,
            Data.LandingData.GroundSpeed,
            Data.LandingData.VerticalSpeed,
            Data.LandingData.bIsPhysicallyInAir ? 1 : 0,
            Data.LandingData.bIsLanding ? 1 : 0,
            Data.LandingData.bLandingRequested ? 1 : 0,
            Data.LandingData.bUseHeavyLand ? 1 : 0,
            Data.LandingData.bLandWasMoving ? 1 : 0,
            Data.LandingData.bLandWasSprinting ? 1 : 0,
            Data.LandingData.LandStartGroundSpeed,
            Data.LandingData.LandStartFallSpeed,
            Data.LandingData.LandMoveDirection.X,
            Data.LandingData.LandMoveDirection.Y,
            Data.LandingData.LandingElapsedTime);
        UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *StateDebugLine);
        AppendMotionMatchingAnimCaptureLine(StateDebugLine);

        const FTransform OwnerComponentTransform = AnimInstanceObj->GetOwningComponent()
            ? AnimInstanceObj->GetOwningComponent()->GetComponentTransform()
            : FTransform::Identity;

        if (Data.AirData.bIsFallOffStart)
        {
            const FString FallOffTrajectoryDebugLine = FString::Printf(
                TEXT("[MMCAP_FALLOFF_TRAJ] Anim=%s Raw={%s %s %s} Final={%s %s %s}"),
                *AnimInstanceObj->GetName(),
                *FormatTrajectorySample(Data.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform, 0.2f),
                *FormatTrajectorySample(Data.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform, 0.5f),
                *FormatTrajectorySample(Data.MovementData.FallOffTrajectoryBefore, OwnerComponentTransform, 1.f),
                *FormatTrajectorySample(Data.MovementData.Trajectory, OwnerComponentTransform, 0.2f),
                *FormatTrajectorySample(Data.MovementData.Trajectory, OwnerComponentTransform, 0.5f),
                *FormatTrajectorySample(Data.MovementData.Trajectory, OwnerComponentTransform, 1.f));
            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *FallOffTrajectoryDebugLine);
            AppendMotionMatchingAnimCaptureLine(FallOffTrajectoryDebugLine);
        }
    }

}

UMotionMatchingAnimInstance::UMotionMatchingAnimInstance()
{
    bUseMultiThreadedAnimationUpdate = true;

    FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
    FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
    FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
    FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
    FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

    FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
    FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
    FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
    FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

FAnimInstanceProxy* UMotionMatchingAnimInstance::CreateAnimInstanceProxy()
{
    return new FMotionMatchingAnimInstanceProxy(this);
}

void UMotionMatchingAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    CachedBasePlayer = Cast<ABasePlayer>(TryGetPawnOwner());
    if (CachedBasePlayer)
    {
        CachedLocomotionStateComponent = CachedBasePlayer->FindComponentByClass<ULocomotionAnimStateComponent>();

        UCharacterTrajectoryComponent* TrajectoryComp = CachedBasePlayer->FindComponentByClass<UCharacterTrajectoryComponent>();
        if (TrajectoryComp)
        {
            TArray<FName> TrajPropNames = { FName("TransformTrajectory"), FName("Trajectory"), FName("QueryTrajectory") };
            for (const FName& PropName : TrajPropNames)
            {
                if (FProperty* Prop = TrajectoryComp->GetClass()->FindPropertyByName(PropName))
                {
                    if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                    {
                        if (StructProp->Struct == FTransformTrajectory::StaticStruct())
                        {
                            CachedTrajectoryProperty = StructProp;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Cache the nodes in the proxy on the game thread
    FMotionMatchingAnimInstanceProxy& MyProxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
    MyProxy.CacheNodes(this);
}

bool UMotionMatchingAnimInstance::IsDedicatedServerAnimationContext() const
{
    return GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer;
}

float UMotionMatchingAnimInstance::CalculateAimOffsetAlpha(const FAnimThreadSafeData& ThreadSafeData) const
{
    if (bForceAimOffsetAlwaysOn)
    {
        return 1.f;
    }

    if (ThreadSafeData.AirData.bIsInAir ||
        ThreadSafeData.LandingData.bIsLanding ||
        (CachedBasePlayer && (CachedBasePlayer->bIsAttacking || CachedBasePlayer->bIsDodging || CachedBasePlayer->bIsHitReacting)))
    {
        return 0.f;
    }

    if (CachedBasePlayer && CachedBasePlayer->bIsCombatMode)
    {
        return CombatAimAlpha;
    }

    if (CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting)
    {
        return SprintAimAlpha;
    }

    return ThreadSafeData.LandingData.GroundSpeed > GenericMoveInputSpeedThreshold ? MovingAimAlpha : StandingAimAlpha;
}

void UMotionMatchingAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!GetWorld() || !GetWorld()->IsGameWorld())
    {
        return;
    }

    if (!CachedBasePlayer)
    {
        CachedBasePlayer = Cast<ABasePlayer>(TryGetPawnOwner());
        if (!CachedBasePlayer) return;
    }

    if (!CachedLocomotionStateComponent)
    {
        CachedLocomotionStateComponent = CachedBasePlayer->FindComponentByClass<ULocomotionAnimStateComponent>();
        if (!CachedLocomotionStateComponent) return;
    }

    // Skip all evaluation and Chooser Table lookups on Dedicated Server
    if (IsDedicatedServerAnimationContext())
    {
        return;
    }

    // 트랙젝토리 틱을 수동으로 구동 (매 프레임 위치/회전 보간 등 물리 계산 진행)
    if (USWTrajectoryComponent* TrajectoryComp = CachedBasePlayer->FindComponentByClass<USWTrajectoryComponent>())
    {
        TrajectoryComp->UpdateTrajectoryState(DeltaSeconds);
    }

    // 최적화 틱 레이트에 맞추어 이번 프레임의 모션 매칭 평가 여부 결정
    if (!ShouldEvaluateMotionMatchingThisFrame(DeltaSeconds))
    {
        return;
    }

    // 1. C++ 직접 상태 분기 및 알맞은 PSD 할당 (Chooser Table 미사용)
    const UPoseSearchDatabase* PrevActiveDB = CurrentActivePoseSearchDatabase.Get();
    CurrentActivePoseSearchDatabase = nullptr;

    if (CachedLocomotionStateComponent)
    {
        const bool bSimulated = (CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy);
        const bool bSprinting = CachedLocomotionStateComponent->bIsSprinting;

        switch (CachedLocomotionStateComponent->CurrentState)
        {
        case ELocomotionState::Idle:
            CurrentActivePoseSearchDatabase = IdleDatabase;
            break;

        case ELocomotionState::Start:
            {
                // 로컬 플레이어의 경우 입력 방향(Control Yaw)과 캐릭터 정면(Actor Yaw)의 각도 차이를 계산하여 Reface 사용 여부 결정
                // 단, 이미 Start 상태에 진입해 있는 동안에는 프레임마다 데이터베이스가 직진용으로 복구되어 중간에 끊기는 것을 방지하기 위해 
                // 이전 프레임에 이미 Reface DB를 할당했다면 해당 선택을 고정(Latch)합니다.
                bool bUseReface = false;
                const bool bAlreadyUsingReface = (PrevActiveDB == RunStartRefaceDatabase.Get() || PrevActiveDB == SprintStartRefaceDatabase.Get());
                
                if (bAlreadyUsingReface)
                {
                    bUseReface = true;
                }
                else if (!bSimulated && CachedBasePlayer)
                {
                    float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
                    float ControlYaw = ActorYaw;
                    if (CachedBasePlayer->GetController())
                    {
                        ControlYaw = CachedBasePlayer->GetController()->GetControlRotation().Yaw;
                    }
                    float YawDiff = FMath::Abs(FRotator::NormalizeAxis(ActorYaw - ControlYaw));
                    bUseReface = YawDiff >= 50.f; // 50도 이상 꺾일 때만 Reface 사용 허용
                }

                if (bSprinting)
                {
                    if (bSimulated)
                    {
                        CurrentActivePoseSearchDatabase = SprintStartDatabaseRemote.Get() ? SprintStartDatabaseRemote : SprintStartDatabase;
                    }
                    else
                    {
                        if (bUseReface)
                        {
                            CurrentActivePoseSearchDatabase = SprintStartRefaceDatabase.Get() ? SprintStartRefaceDatabase : SprintStartDatabase;
                        }
                        else
                        {
                            CurrentActivePoseSearchDatabase = SprintStartDatabase;
                        }
                    }
                }
                else
                {
                    if (bSimulated)
                    {
                        CurrentActivePoseSearchDatabase = StartDatabaseRemote.Get() ? StartDatabaseRemote : StartDatabase;
                    }
                    else
                    {
                        if (bUseReface)
                        {
                            CurrentActivePoseSearchDatabase = RunStartRefaceDatabase.Get() ? RunStartRefaceDatabase : StartDatabase;
                        }
                        else
                        {
                            CurrentActivePoseSearchDatabase = StartDatabase;
                        }
                    }
                }
            }
            break;

        case ELocomotionState::Locomotion:
            if (bSprinting)
            {
                CurrentActivePoseSearchDatabase = SprintLocomotionDatabase;
            }
            else
            {
                // 방향 전환 과도기 상태일 때는 곡선/피벗 에셋들이 있는 LocomotionTransitionDatabase 적용
                const bool bTransitioning = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->bIsLocomotionTransitioning : false;
                if (bTransitioning && LocomotionTransitionDatabase)
                {
                    CurrentActivePoseSearchDatabase = (bSimulated && LocomotionTransitionDatabaseRemote)
                        ? LocomotionTransitionDatabaseRemote
                        : LocomotionTransitionDatabase;
                }
                else
                {
                    CurrentActivePoseSearchDatabase = (bSimulated && LocomotionDatabaseRemote)
                        ? LocomotionDatabaseRemote
                        : LocomotionDatabase;
                }
            }
            break;

        case ELocomotionState::Stop:
            CurrentActivePoseSearchDatabase = bSprinting ? SprintStopDatabase : StopDatabase;
            break;

        case ELocomotionState::InAir:
            if (CachedLocomotionStateComponent->bIsJumping)
            {
                CurrentActivePoseSearchDatabase = JumpStartDatabase;
            }
            else if (CachedLocomotionStateComponent->bIsFallOffStart)
            {
                CurrentActivePoseSearchDatabase = FallOffDatabase;
            }
            else
            {
                CurrentActivePoseSearchDatabase = InAirDatabase;
            }
            break;

        case ELocomotionState::Landing:
            {
                const bool bLandMoving = CachedLocomotionStateComponent->bLandWasMoving;
                const bool bLandSprinting = CachedLocomotionStateComponent->bLandWasSprinting;
                const bool bHeavy = CachedLocomotionStateComponent->bUseHeavyLand;

                if (bLandMoving)
                {
                    if (bLandSprinting)
                    {
                        CurrentActivePoseSearchDatabase = bHeavy ? SprintLandHeavyDatabase : SprintLandLightDatabase;
                    }
                    else
                    {
                        CurrentActivePoseSearchDatabase = bHeavy ? RunLandHeavyDatabase : RunLandLightDatabase;
                    }
                }
                else
                {
                    CurrentActivePoseSearchDatabase = bHeavy ? StandLandHeavyDatabase : StandLandLightDatabase;
                }
            }
            break;

        default:
            CurrentActivePoseSearchDatabase = IdleDatabase;
            break;
        }
    }

    // 2. Start/Stop/Landing 상태의 애니메이션 재생 완료 감지 및 자동 상태 전환 처리 (노티파이 의존성 100% 제거)
    if (CachedLocomotionStateComponent)
    {
        const ELocomotionState State = CachedLocomotionStateComponent->CurrentState;
        if (IsTransitionMotionMatchingState(State))
        {
            if (!bTransitionLocked || LockedTransitionState != State || !LockedTransitionDatabase)
            {
                LockedTransitionDatabase = CurrentActivePoseSearchDatabase;
                LockedTransitionState = State;
                bTransitionLocked = true;
            }
            else
            {
                CurrentActivePoseSearchDatabase = LockedTransitionDatabase;
            }
        }
        else
        {
            LockedTransitionDatabase = nullptr;
            LockedTransitionState = ELocomotionState::Idle;
            bTransitionLocked = false;
        }
    }

    // Simulated proxies follow replicated state; do not locally finish transition states.
    if (CachedLocomotionStateComponent && CachedBasePlayer && CachedBasePlayer->GetLocalRole() != ROLE_SimulatedProxy)
    {
        const ELocomotionState State = CachedLocomotionStateComponent->CurrentState;
        if (State == ELocomotionState::Start || State == ELocomotionState::Stop || State == ELocomotionState::Landing)
        {
            FMotionMatchingAnimInstanceProxy& MyProxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
            for (const FCachedMotionMatchingNodeInfo& Info : MyProxy.CachedMMNodes)
            {
                if (Info.NodeProperty)
                {
                    FAnimNode_MotionMatching* MMNode = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(this);
                    if (MMNode)
                    {
                        const FPoseSearchBlueprintResult& Result = MMNode->GetMotionMatchingState().SearchResult;
                        if (Result.SelectedAnim)
                        {
                            UAnimSequence* AnimSeq = Cast<UAnimSequence>(Result.SelectedAnim);
                            if (AnimSeq)
                            {
                                float PlayLength = AnimSeq->GetPlayLength();
                                float CurrentTime = Result.SelectedTime;
                                
                                // 일반 에셋은 종료 0.1초 전에 전이, 단 아주 짧은 에셋(0.4초 이하)은 종료 0.03초 전에 완료 유도
                                const float TransitionOffset = (PlayLength < 0.4f) ? 0.03f : 0.10f;
                                const float TransitionThreshold = PlayLength - TransitionOffset;

                                if (CurrentTime >= TransitionThreshold)
                                {
                                    if (State == ELocomotionState::Start)
                                    {
                                        CachedLocomotionStateComponent->NotifyStartFinished();
                                    }
                                    else if (State == ELocomotionState::Stop)
                                    {
                                        CachedLocomotionStateComponent->NotifyStopFinished();
                                    }
                                    else if (State == ELocomotionState::Landing)
                                    {
                                        CachedLocomotionStateComponent->NotifyLandingFinished();
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Pack data into thread-safe struct
    FAnimThreadSafeData ThreadSafeData;

    // Movement Data
    ThreadSafeData.MovementData.Velocity = CachedLocomotionStateComponent->Velocity;
    ThreadSafeData.MovementData.VelocityLocal = CachedBasePlayer->GetActorTransform().InverseTransformVectorNoScale(CachedLocomotionStateComponent->Velocity);
    ThreadSafeData.MovementData.Acceleration = CachedLocomotionStateComponent->Acceleration;
    const bool bIsFallOffForDebug =
        CachedLocomotionStateComponent->CurrentState == ELocomotionState::InAir &&
        CachedLocomotionStateComponent->bIsFallOffStart &&
        !CachedLocomotionStateComponent->bIsJumping;
    if (bIsFallOffForDebug)
    {
        FallOffDebugElapsedTime = bWasFallOffForDebug ? FallOffDebugElapsedTime + DeltaSeconds : 0.0f;
    }
    else
    {
        FallOffDebugElapsedTime = 0.0f;
    }
    bWasFallOffForDebug = bIsFallOffForDebug;
    ThreadSafeData.MovementData.FallOffElapsedTime = FallOffDebugElapsedTime;
    
    UCharacterTrajectoryComponent* TrajectoryComp = CachedBasePlayer->FindComponentByClass<UCharacterTrajectoryComponent>();
    if (TrajectoryComp)
    {
        // Cache the trajectory property on demand if it wasn't cached during initialization
        if (!CachedTrajectoryProperty)
        {
            TArray<FName> TrajPropNames = { FName("TransformTrajectory"), FName("Trajectory"), FName("QueryTrajectory") };
            for (const FName& PropName : TrajPropNames)
            {
                if (FProperty* Prop = TrajectoryComp->GetClass()->FindPropertyByName(PropName))
                {
                    if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                    {
                        if (StructProp->Struct == FTransformTrajectory::StaticStruct())
                        {
                            CachedTrajectoryProperty = StructProp;
                            break;
                        }
                    }
                }
            }
        }

        if (CachedTrajectoryProperty)
        {
            void* PropPtr = CachedTrajectoryProperty->ContainerPtrToValuePtr<void>(TrajectoryComp);
            if (PropPtr)
            {
                CachedTrajectoryProperty->Struct->CopyScriptStruct(&ThreadSafeData.MovementData.Trajectory, PropPtr);
                ThreadSafeData.MovementData.FallOffTrajectoryBefore = ThreadSafeData.MovementData.Trajectory;

                // Trajectory Facing Post-processing to solve Turn-in-Place and Strafe awkwardness
                if (CachedLocomotionStateComponent)
                {
                    ELocomotionState State = CachedLocomotionStateComponent->CurrentState;

                    if (State == ELocomotionState::InAir && CachedLocomotionStateComponent->bIsFallOffStart)
                    {
                        ApplyFallingPredictionToTrajectory(ThreadSafeData.MovementData.Trajectory, *CachedBasePlayer);
                    }
                    else if (State == ELocomotionState::Landing
                        && CachedLocomotionStateComponent->bLandWasMoving
                        && !CachedLocomotionStateComponent->LandMoveDirection.IsNearlyZero())
                    {
                        const bool bSimulatedProxy = CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy;
                        if (bSimulatedProxy)
                        {
                            const float LandingGroundZ = GetOwningComponent()
                                ? GetOwningComponent()->GetComponentLocation().Z
                                : CachedBasePlayer->GetActorLocation().Z;

                            TArray<FTransformTrajectorySample>& Samples = ThreadSafeData.MovementData.Trajectory.Samples;
                            for (FTransformTrajectorySample& Sample : Samples)
                            {
                                if (Sample.TimeInSeconds >= 0.f)
                                {
                                    FTransform SampleTransform = Sample.GetTransform();
                                    FVector SampleLocation = SampleTransform.GetLocation();
                                    SampleLocation.Z = LandingGroundZ;
                                    SampleTransform.SetLocation(SampleLocation);
                                    Sample.SetTransform(SampleTransform);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Input Data
    ThreadSafeData.InputData.MoveInputSize = CachedLocomotionStateComponent->MoveInputSize;
    ThreadSafeData.InputData.bHasMoveInput = CachedLocomotionStateComponent->bHasMoveInput;
    ThreadSafeData.InputData.bSharpTurnRequested = CachedLocomotionStateComponent->bSharpTurnRequested;
    ThreadSafeData.InputData.MoveInput = CachedLocomotionStateComponent->CachedMoveInput;

    // Ground Data
    ThreadSafeData.GroundData.bStartRequested = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start);
    ThreadSafeData.GroundData.bStopRequested = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop);
    ThreadSafeData.GroundData.GroundMotionMode = static_cast<uint8>(CachedLocomotionStateComponent->CurrentState);

    // Air Data
    ThreadSafeData.AirData.bIsInAir = CachedLocomotionStateComponent->bIsInAir;
    ThreadSafeData.AirData.bIsJumping = CachedLocomotionStateComponent->bIsJumping;
    ThreadSafeData.AirData.bIsFallOffStart = CachedLocomotionStateComponent->bIsFallOffStart;

    // Landing Data
    ThreadSafeData.LandingData.bIsLanding = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Landing);
    ThreadSafeData.LandingData.bUseHeavyLand = CachedLocomotionStateComponent->bUseHeavyLand;
    ThreadSafeData.LandingData.LastFallSpeed = CachedLocomotionStateComponent->LastFallSpeed;
    ThreadSafeData.LandingData.GroundSpeed = CachedLocomotionStateComponent->GroundSpeed;
    ThreadSafeData.LandingData.VerticalSpeed = CachedLocomotionStateComponent->VerticalSpeed;
    ThreadSafeData.LandingData.LandStartGroundSpeed = CachedLocomotionStateComponent->LandStartGroundSpeed;
    ThreadSafeData.LandingData.LandStartFallSpeed = CachedLocomotionStateComponent->LandStartFallSpeed;
    ThreadSafeData.LandingData.LandingElapsedTime = CachedLocomotionStateComponent->LandingElapsedTime;
    ThreadSafeData.LandingData.LandMoveDirection = CachedLocomotionStateComponent->LandMoveDirection;
    ThreadSafeData.LandingData.bIsPhysicallyInAir = CachedLocomotionStateComponent->bIsPhysicallyInAir;
    ThreadSafeData.LandingData.bLandingRequested = CachedLocomotionStateComponent->bLandingRequested;
    ThreadSafeData.LandingData.bLandWasMoving = CachedLocomotionStateComponent->bLandWasMoving;
    ThreadSafeData.LandingData.bLandWasSprinting = CachedLocomotionStateComponent->bLandWasSprinting;

    if (CachedBasePlayer->GetController())
    {
        const FRotator ActorRotation = CachedBasePlayer->GetActorRotation();
        const FRotator ControlRotation = CachedBasePlayer->GetControlRotation();
        ThreadSafeData.AimData.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, ControlRotation.Yaw), -MaxAimYaw, MaxAimYaw);
        ThreadSafeData.AimData.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(ControlRotation.Pitch), -MaxAimPitch, MaxAimPitch);
        ThreadSafeData.AimData.AimOffsetAlpha = CalculateAimOffsetAlpha(ThreadSafeData);
    }

    ThreadSafeData.WeaponUpperBodyData = FAnimWeaponUpperBodyData();

    if (const UPlayerEquipmentComponent* EquipmentComponent = CachedBasePlayer->GetEquipmentComponent())
    {
        const FGameplayTag EquippedWeaponTag = EquipmentComponent->GetEquippedItemTag();
        const FGameplayTag OverlayTag = EquipmentComponent->GetEquippedUpperBodyOverlayTag();
        const bool bHasWeaponEquipped = EquippedWeaponTag.IsValid();
        const bool bUseWeaponOverlay = OverlayTag.IsValid();
        const bool bGroundedForOverlay =
            CachedLocomotionStateComponent->CurrentState == ELocomotionState::Idle ||
            CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start ||
            CachedLocomotionStateComponent->CurrentState == ELocomotionState::Locomotion ||
            CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop;
        const float GroundSpeed = CachedLocomotionStateComponent->GroundSpeed;
        const bool bMoving = GroundSpeed > WeaponUpperBodyMovingSpeedThreshold;
        const bool bSprinting = CachedLocomotionStateComponent->bIsSprinting;

        ThreadSafeData.WeaponUpperBodyData.bHasWeaponEquipped = bHasWeaponEquipped;
        ThreadSafeData.WeaponUpperBodyData.EquippedWeaponTag = EquippedWeaponTag;
        ThreadSafeData.WeaponUpperBodyData.OverlayTag = OverlayTag;
        ThreadSafeData.WeaponUpperBodyData.OverlayIndex = EquipmentComponent->GetEquippedUpperBodyOverlayIndex();
        ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody = bEnableWeaponUpperBodyOverlay && bUseWeaponOverlay && bGroundedForOverlay;
        ThreadSafeData.WeaponUpperBodyData.UpperBodyAlpha = ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody ? 1.f : 0.f;
        ThreadSafeData.WeaponUpperBodyData.GroundSpeed = GroundSpeed;
        ThreadSafeData.WeaponUpperBodyData.bIsSprinting = bSprinting;

        if (!ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody)
        {
            ThreadSafeData.WeaponUpperBodyData.OverlayState = EWeaponUpperBodyOverlayState::None;
        }
        else if (bSprinting)
        {
            ThreadSafeData.WeaponUpperBodyData.OverlayState = EWeaponUpperBodyOverlayState::Sprint;
        }
        else if (bMoving)
        {
            ThreadSafeData.WeaponUpperBodyData.OverlayState = EWeaponUpperBodyOverlayState::Run;
        }
        else
        {
            ThreadSafeData.WeaponUpperBodyData.OverlayState = EWeaponUpperBodyOverlayState::Idle;
        }
    }

    // 4. Push variables safely to the proxy
    FMotionMatchingAnimInstanceProxy& MyProxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
    MyProxy.ThreadSafeData = ThreadSafeData;
    MyProxy.CurrentActivePoseSearchDatabase = CurrentActivePoseSearchDatabase;

    // Debug logging for both the local pawn and observed simulated proxies.
    if (CVarMotionMatchingDebugLogging.GetValueOnGameThread() > 0 && CachedBasePlayer)
    {
        StateLogTimer += DeltaSeconds;

        FName CurrentDatabaseName = CurrentActivePoseSearchDatabase ? CurrentActivePoseSearchDatabase->GetFName() : NAME_None;

        bool bChanged = (CachedLocomotionStateComponent && CachedLocomotionStateComponent->CurrentState != LastState) || (CurrentDatabaseName != LastDatabaseName);
        const ENetRole LocalRole = CachedBasePlayer->GetLocalRole();
        const ENetMode NetMode = CachedBasePlayer->GetNetMode();
        const bool bIsLocalPawn = CachedBasePlayer->IsLocallyControlled();
        const bool bIsSimulatedProxy = LocalRole == ROLE_SimulatedProxy;
        const FString PawnName = CachedBasePlayer->GetName();
        const bool bRemoteUsesReplicatedInput =
            bIsSimulatedProxy &&
            CachedBasePlayer->LocomotionStateSnapshot.bHasMoveInput;
        const TCHAR* InputSourceText = bIsLocalPawn
            ? TEXT("EnhancedInput")
            : (bRemoteUsesReplicatedInput ? TEXT("ReplicatedInput") : TEXT("VelocityEstimate"));
        const FString SnapshotStateStr = TEXT("N/A");

        if (bChanged)
        {
            FString OldStateStr = StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)LastState);
            FString NewStateStr = CachedLocomotionStateComponent ? StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)CachedLocomotionStateComponent->CurrentState) : TEXT("None");
            UE_LOG(LogTemp, Log, TEXT("[MM_STATE_CHANGE] Pawn=%s Net=%s Role=%s Local=%d Sim=%d State changed: %s -> %s | Database: %s Snapshot=%s Seq=%d"),
                *PawnName,
                FormatNetMode(NetMode),
                FormatNetRole(LocalRole),
                bIsLocalPawn ? 1 : 0,
                bIsSimulatedProxy ? 1 : 0,
                *OldStateStr,
                *NewStateStr,
                *CurrentDatabaseName.ToString(),
                *SnapshotStateStr,
                CachedBasePlayer->LocomotionStateSnapshot.EventSequence);

            if (CachedLocomotionStateComponent)
            {
                const FString PsdDebugLine = FString::Printf(
                    TEXT("[MMCAP_PSD] Pawn=%s Net=%s Role=%s Local=%d Sim=%d Auth=%d PrevState=%s State=%s SnapshotState=%s Seq=%d Database=%s Jump=%d FallOff=%d Landing=%d LandingRequested=%d Heavy=%d LandMoving=%d SprintLand=%d LandTime=%.3f LandSpeed=%.1f FallSpeed=%.1f Input=(R=%.2f,F=%.2f) InputSource=%s"),
                    *PawnName,
                    FormatNetMode(NetMode),
                    FormatNetRole(LocalRole),
                    bIsLocalPawn ? 1 : 0,
                    bIsSimulatedProxy ? 1 : 0,
                    CachedBasePlayer->HasAuthority() ? 1 : 0,
                    *OldStateStr,
                    *NewStateStr,
                    *SnapshotStateStr,
                    CachedBasePlayer->LocomotionStateSnapshot.EventSequence,
                    *CurrentDatabaseName.ToString(),
                    CachedLocomotionStateComponent->bIsJumping ? 1 : 0,
                    CachedLocomotionStateComponent->bIsFallOffStart ? 1 : 0,
                    CachedLocomotionStateComponent->bIsLanding ? 1 : 0,
                    CachedLocomotionStateComponent->bLandingRequested ? 1 : 0,
                    CachedLocomotionStateComponent->bUseHeavyLand ? 1 : 0,
                    CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
                    CachedLocomotionStateComponent->bLandWasSprinting ? 1 : 0,
                    CachedLocomotionStateComponent->LandingElapsedTime,
                    CachedLocomotionStateComponent->LandStartGroundSpeed,
                    CachedLocomotionStateComponent->LandStartFallSpeed,
                    CachedLocomotionStateComponent->CachedMoveInput.X,
                    CachedLocomotionStateComponent->CachedMoveInput.Y,
                    InputSourceText);
                UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *PsdDebugLine);
                AppendMotionMatchingAnimCaptureLine(PsdDebugLine);

                if (LastState == ELocomotionState::Landing && CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start)
                {
                    const FString LandToStartDebugLine = FString::Printf(
                        TEXT("[MMCAP_LAND_TO_START] Landing selected Start/run_Start PSD. Database=%s HasInput=%d MoveInputHeld=%.3f Ground=%.1f LandWasMoving=%d LandTime=%.3f MinLandTime=%.3f Input=(R=%.2f,F=%.2f)"),
                        *CurrentDatabaseName.ToString(),
                        CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0,
                        CachedLocomotionStateComponent->MoveInputHeldTime,
                        CachedLocomotionStateComponent->GroundSpeed,
                        CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
                        CachedLocomotionStateComponent->LandingElapsedTime,
                        CachedLocomotionStateComponent->MinimumLandingDuration,
                        CachedLocomotionStateComponent->CachedMoveInput.X,
                        CachedLocomotionStateComponent->CachedMoveInput.Y);
                    UE_LOG(LogMotionMatchingCapture, Warning, TEXT("%s"), *LandToStartDebugLine);
                    AppendMotionMatchingAnimCaptureLine(LandToStartDebugLine);
                }
            }
        }

        if (bChanged && CVarMotionMatchingDebugLogging.GetValueOnGameThread() < 2)
        {
            if (CachedLocomotionStateComponent)
            {
                LastState = CachedLocomotionStateComponent->CurrentState;
            }
            LastDatabaseName = CurrentDatabaseName;
        }

        if ((bChanged || StateLogTimer >= 0.2f) && CVarMotionMatchingDebugLogging.GetValueOnGameThread() >= 2)
        {
            StateLogTimer = 0.0f;
            if (CachedLocomotionStateComponent)
            {
                LastState = CachedLocomotionStateComponent->CurrentState;
            }
            LastDatabaseName = CurrentDatabaseName;

            FVector2D MoveInput = CachedLocomotionStateComponent
                ? CachedLocomotionStateComponent->CachedMoveInput
                : (CachedBasePlayer->GetAnimStateComponent() ? CachedBasePlayer->GetAnimStateComponent()->CachedMoveInput : FVector2D::ZeroVector);
            float ControlYaw = CachedBasePlayer->GetControlRotation().Yaw;
            float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
            float YawDelta = FMath::FindDeltaAngleDegrees(ActorYaw, ControlYaw);

            bool bOrient = false;
            bool bUseControllerDesired = false;
            FVector Velocity = FVector::ZeroVector;
            FVector Acceleration = FVector::ZeroVector;
            TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
            if (UCharacterMovementComponent* MoveComp = CachedBasePlayer->GetCharacterMovement())
            {
                bOrient = MoveComp->bOrientRotationToMovement;
                bUseControllerDesired = MoveComp->bUseControllerDesiredRotation;
                Velocity = MoveComp->Velocity;
                Acceleration = MoveComp->GetCurrentAcceleration();
                MovementMode = MoveComp->MovementMode;
            }

            const TArray<FTransformTrajectorySample>& Samples = ThreadSafeData.MovementData.Trajectory.Samples;
            FTransform ActorTransform = CachedBasePlayer->GetActorTransform();

            auto FindClosestSample = [&Samples](float TargetTime) -> const FTransformTrajectorySample*
            {
                const FTransformTrajectorySample* Closest = nullptr;
                float MinDiff = FLT_MAX;
                for (const FTransformTrajectorySample& Sample : Samples)
                {
                    float Diff = FMath::Abs(Sample.TimeInSeconds - TargetTime);
                    if (Diff < MinDiff)
                    {
                        MinDiff = Diff;
                        Closest = &Sample;
                    }
                }
                return Closest;
            };

            auto FormatSample = [&FindClosestSample, &ActorTransform](float Time) -> FString
            {
                const FTransformTrajectorySample* Sample = FindClosestSample(Time);
                if (!Sample) return FString::Printf(TEXT("[T=%.1fs: N/A]"), Time);
                FTransform RelativeTransform = Sample->GetTransform().GetRelativeTransform(ActorTransform);
                FVector Loc = RelativeTransform.GetLocation();
                float Yaw = RelativeTransform.GetRotation().Rotator().Yaw;
                return FString::Printf(TEXT("[T=%.1fs: Pos=(X=%.1f, Y=%.1f), Yaw=%.1f]"), Time, Loc.X, Loc.Y, Yaw);
            };

            FString TrajHistoryStr = FormatSample(-0.5f);
            FString TrajCurrentStr = FormatSample(0.0f);
            FString TrajFuture1Str = FormatSample(0.2f);
            FString TrajFuture2Str = FormatSample(0.5f);
            FString TrajFuture3Str = FormatSample(1.0f);

            bool bInstantSnap = false;
            float MeshOffset = 0.f;
            if (CachedLocomotionStateComponent)
            {
                bInstantSnap = CachedLocomotionStateComponent->GetUseInstantRotationSnap();
                MeshOffset = CachedLocomotionStateComponent->GetMeshYawOffset();
            }

            const FString FrameDebugLine = FString::Printf(
                TEXT("[MMCAP_FRAME] Pawn=%s Net=%s Role=%s Local=%d Sim=%d Auth=%d State=%s SnapshotState=%s Seq=%d Database=%s Sprint=%d InputSource=%s Input=(R=%.2f,F=%.2f) HasInput=%d MoveHeld=%.3f Speed=%.1f Vel=(%.1f,%.1f,%.1f) Accel=(%.1f,%.1f,%.1f) MoveMode=%d ControlYaw=%.1f ActorYaw=%.1f YawDelta=%.1f OrientToMove=%d UseControllerDesired=%d InstantSnap=%d MeshYawOffset=%.1f Traj=%s %s %s %s %s"),
                *PawnName,
                FormatNetMode(NetMode),
                FormatNetRole(LocalRole),
                bIsLocalPawn ? 1 : 0,
                bIsSimulatedProxy ? 1 : 0,
                CachedBasePlayer->HasAuthority() ? 1 : 0,
                CachedLocomotionStateComponent ? *StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)CachedLocomotionStateComponent->CurrentState) : TEXT("None"),
                *SnapshotStateStr,
                CachedBasePlayer->LocomotionStateSnapshot.EventSequence,
                *CurrentDatabaseName.ToString(),
                (CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting) ? 1 : 0,
                InputSourceText,
                MoveInput.X,
                MoveInput.Y,
                CachedLocomotionStateComponent && CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0,
                CachedLocomotionStateComponent ? CachedLocomotionStateComponent->MoveInputHeldTime : 0.f,
                CachedLocomotionStateComponent ? CachedLocomotionStateComponent->GroundSpeed : 0.f,
                Velocity.X,
                Velocity.Y,
                Velocity.Z,
                Acceleration.X,
                Acceleration.Y,
                Acceleration.Z,
                static_cast<int32>(MovementMode.GetValue()),
                ControlYaw,
                ActorYaw,
                YawDelta,
                bOrient ? 1 : 0,
                bUseControllerDesired ? 1 : 0,
                bInstantSnap ? 1 : 0,
                MeshOffset,
                *TrajHistoryStr,
                *TrajCurrentStr,
                *TrajFuture1Str,
                *TrajFuture2Str,
                *TrajFuture3Str);
            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *FrameDebugLine);
            AppendMotionMatchingAnimCaptureLine(FrameDebugLine);

            UAnimMontage* ActiveMontage = GetCurrentActiveMontage();
            const FString GraphDebugLine = FString::Printf(
                TEXT("[MMCAP_GRAPH] Pawn=%s Net=%s Role=%s AnimInstance=%s ActiveMontage=%s MontagePosition=%.3f MontagePlaying=%d"),
                *PawnName,
                FormatNetMode(NetMode),
                FormatNetRole(LocalRole),
                *GetName(),
                *GetNameSafe(ActiveMontage),
                ActiveMontage ? Montage_GetPosition(ActiveMontage) : 0.f,
                ActiveMontage && Montage_IsPlaying(ActiveMontage) ? 1 : 0);
            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *GraphDebugLine);
            AppendMotionMatchingAnimCaptureLine(GraphDebugLine);
        }
    }

}

UPoseSearchDatabase* UMotionMatchingAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
    return CurrentActivePoseSearchDatabase;
}

float UMotionMatchingAnimInstance::GetThreadSafeAimYaw() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.AimData.AimYaw;
}

float UMotionMatchingAnimInstance::GetThreadSafeAimPitch() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.AimData.AimPitch;
}

float UMotionMatchingAnimInstance::GetThreadSafeAimOffsetAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.AimData.AimOffsetAlpha;
}

bool UMotionMatchingAnimInstance::GetThreadSafeHasBowEquipped() const
{
    const FGameplayTag OverlayTag = GetThreadSafeWeaponUpperBodyOverlayTag();
    const FGameplayTag EquippedWeaponTag = GetThreadSafeEquippedWeaponTag();
    return OverlayTag.MatchesTag(Item_Weapon_Bow) || EquippedWeaponTag.MatchesTag(Item_Weapon_Bow);
}

bool UMotionMatchingAnimInstance::GetThreadSafeHasWeaponEquipped() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.bHasWeaponEquipped;
}

FGameplayTag UMotionMatchingAnimInstance::GetThreadSafeEquippedWeaponTag() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.EquippedWeaponTag;
}

FGameplayTag UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyOverlayTag() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.OverlayTag;
}

int32 UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyOverlayIndex() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.OverlayIndex;
}

bool UMotionMatchingAnimInstance::GetThreadSafeShouldOverrideWeaponUpperBody() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody;
}

EWeaponUpperBodyOverlayMode UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyMode() const
{
    const FAnimWeaponUpperBodyData& WeaponData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData;
    if (!WeaponData.OverlayTag.MatchesTag(Item_Weapon_Bow))
    {
        return EWeaponUpperBodyOverlayMode::None;
    }

    switch (WeaponData.OverlayState)
    {
    case EWeaponUpperBodyOverlayState::Idle:
        return EWeaponUpperBodyOverlayMode::BowIdle;
    case EWeaponUpperBodyOverlayState::Run:
        return EWeaponUpperBodyOverlayMode::BowRun;
    case EWeaponUpperBodyOverlayState::Sprint:
        return EWeaponUpperBodyOverlayMode::BowSprint;
    default:
        return EWeaponUpperBodyOverlayMode::None;
    }
}

EWeaponUpperBodyOverlayState UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyState() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.OverlayState;
}

float UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.UpperBodyAlpha;
}

float UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodySpeed() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.GroundSpeed;
}

FFootPlacementPlantSettings UMotionMatchingAnimInstance::Get_FootPlacementPlantSettings() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.GroundData.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault;
}

FFootPlacementInterpolationSettings UMotionMatchingAnimInstance::Get_FootPlacementInterpolationSettings() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.GroundData.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault;
}

bool UMotionMatchingAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds)
{
    if (!CachedBasePlayer || IsDedicatedServerAnimationContext())
    {
        MotionMatchingUpdateAccumulator = 0.0f;
        return false;
    }

    // 로컬 컨트롤러에 의해 지배받는 경우 무조건 틱 수행
    if (CachedBasePlayer->IsLocallyControlled())
    {
        MotionMatchingUpdateAccumulator = 0.0f;
        return true;
    }

    // 시각적으로 렌더링되고 있지 않다면 틱 조절
    const bool bRecentlyRendered = CachedBasePlayer->WasRecentlyRendered(RecentlyRenderedTolerance);
    if (!bRecentlyRendered)
    {
        MotionMatchingUpdateAccumulator += DeltaSeconds;
        if (MotionMatchingUpdateAccumulator < HiddenRemoteUpdateInterval)
        {
            if (CVarMotionMatchingDebugLogging.GetValueOnGameThread() >= 2)
            {
                const FString SkipDebugLine = FString::Printf(
                    TEXT("[MMCAP_SKIP] Pawn=%s Net=%s Role=%s Reason=HiddenRemote Accum=%.3f Required=%.3f RecentlyRendered=0"),
                    *CachedBasePlayer->GetName(),
                    FormatNetMode(CachedBasePlayer->GetNetMode()),
                    FormatNetRole(CachedBasePlayer->GetLocalRole()),
                    MotionMatchingUpdateAccumulator,
                    HiddenRemoteUpdateInterval);
                UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *SkipDebugLine);
                AppendMotionMatchingAnimCaptureLine(SkipDebugLine);
            }
            return false;
        }
        MotionMatchingUpdateAccumulator = 0.0f;
        return true;
    }

    // 카메라와의 거리에 따른 틱 감쇄
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
    if (!LocalPawn)
    {
        MotionMatchingUpdateAccumulator = 0.0f;
        return true;
    }

    const float Distance = FVector::Dist(CachedBasePlayer->GetActorLocation(), LocalPawn->GetActorLocation());
    float UpdateInterval = 0.0f;

    if (Distance > FarMotionMatchingDistance)
    {
        UpdateInterval = FarMotionMatchingUpdateInterval;
    }
    else if (Distance > MidMotionMatchingDistance)
    {
        UpdateInterval = FarMotionMatchingUpdateInterval;
    }
    else if (Distance > NearMotionMatchingDistance)
    {
        UpdateInterval = MidMotionMatchingUpdateInterval;
    }

    if (UpdateInterval <= 0.0f)
    {
        MotionMatchingUpdateAccumulator = 0.0f;
        return true;
    }

    MotionMatchingUpdateAccumulator += DeltaSeconds;
    if (MotionMatchingUpdateAccumulator < UpdateInterval)
    {
        if (CVarMotionMatchingDebugLogging.GetValueOnGameThread() >= 2)
        {
            const FString SkipDebugLine = FString::Printf(
                TEXT("[MMCAP_SKIP] Pawn=%s Net=%s Role=%s Reason=DistanceThrottle Distance=%.1f Accum=%.3f Required=%.3f"),
                *CachedBasePlayer->GetName(),
                FormatNetMode(CachedBasePlayer->GetNetMode()),
                FormatNetRole(CachedBasePlayer->GetLocalRole()),
                Distance,
                MotionMatchingUpdateAccumulator,
                UpdateInterval);
            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *SkipDebugLine);
            AppendMotionMatchingAnimCaptureLine(SkipDebugLine);
        }
        return false;
    }

    MotionMatchingUpdateAccumulator = 0.0f;
    return true;
}
