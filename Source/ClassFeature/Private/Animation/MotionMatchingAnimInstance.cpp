#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Animation/SWTrajectoryComponent.h"
#include "SwimmingComponent.h"
#include "BaseGameplayTags.h"
#include "CharacterTrajectoryComponent.h"
#include "ChooserFunctionLibrary.h"
#include "Chooser.h"
#include "ChooserTypes.h"
#include "IObjectChooser.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Item/Components/BowComponent.h"
#include "Item/Weapons/BowItem.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<int32> CVarAnimStateControllerDebug(
    TEXT("a.StateControllerDebug"),
    0,
    TEXT("State Controller one-shot/TIP diagnostics. 0: Disabled, 1: event trace plus TIP yaw samples."),
    ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarMotionMatchingDebugLogging(
    TEXT("p.MMDebugging"),
    0,
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

    EMovementDirection ResolveStateControllerDirectionFromInput(const FVector2D& MoveInput)
    {
        if (MoveInput.IsNearlyZero())
        {
            return EMovementDirection::Forward;
        }

        // Input is character-local: X=right, Y=forward.  Unlike velocity this
        // is valid on the very first Start frame, before acceleration exists.
        const float Direction = FMath::RadiansToDegrees(FMath::Atan2(MoveInput.X, MoveInput.Y));
        if (Direction >= -22.5f && Direction <= 22.5f) return EMovementDirection::Forward;
        if (Direction > 22.5f && Direction <= 67.5f) return EMovementDirection::ForwardRight;
        if (Direction > 67.5f && Direction <= 112.5f) return EMovementDirection::Right;
        if (Direction > 112.5f && Direction <= 157.5f) return EMovementDirection::BackwardRight;
        if (Direction < -22.5f && Direction >= -67.5f) return EMovementDirection::ForwardLeft;
        if (Direction < -67.5f && Direction >= -112.5f) return EMovementDirection::Left;
        if (Direction < -112.5f && Direction >= -157.5f) return EMovementDirection::BackwardLeft;
        return EMovementDirection::Backward;
    }
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

    bool IsJumpStartAnimation(const UAnimationAsset* AnimationAsset)
    {
        const FString AssetName = GetNameSafe(AnimationAsset);
        return AssetName.Contains(TEXT("_Jump_")) && AssetName.Contains(TEXT("_Start"));
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

    bool CollapseBlendStackToTopPlayer(FAnimNode_MotionMatching& MotionMatchingNode)
    {
        if (MotionMatchingNode.AnimPlayers.Num() <= 1)
        {
            return false;
        }

        MotionMatchingNode.AnimPlayers.RemoveAt(1, MotionMatchingNode.AnimPlayers.Num() - 1, EAllowShrinking::No);
        return true;
    }

    bool StabilizeJumpStartBlendStackPlayer(
        const FAnimationUpdateContext& Context,
        FAnimNode_MotionMatching& MotionMatchingNode,
        FCachedMotionMatchingNodeInfo& NodeInfo)
    {
        if (MotionMatchingNode.AnimPlayers.IsEmpty())
        {
            NodeInfo.bHasJumpStartLock = false;
            NodeInfo.LockedJumpStartAnim.Reset();
            NodeInfo.LockedJumpStartTime = 0.f;
            return false;
        }

        FBlendStackAnimPlayer& TopPlayer = MotionMatchingNode.AnimPlayers[0];
        UAnimationAsset* TopAsset = TopPlayer.GetAnimationAsset();
        if (!IsJumpStartAnimation(TopAsset))
        {
            NodeInfo.bHasJumpStartLock = false;
            NodeInfo.LockedJumpStartAnim.Reset();
            NodeInfo.LockedJumpStartTime = 0.f;
            return false;
        }

        if (!NodeInfo.bHasJumpStartLock || !NodeInfo.LockedJumpStartAnim.IsValid())
        {
            NodeInfo.LockedJumpStartAnim = TopAsset;
            NodeInfo.LockedJumpStartTime = ClampAnimationAssetTime(TopAsset, TopPlayer.GetAccumulatedTime());
            NodeInfo.bHasJumpStartLock = true;
            NodeInfo.bPostUpdateCollapsedTransitionStack = CollapseBlendStackToTopPlayer(MotionMatchingNode);
            return false;
        }

        UAnimationAsset* LockedAsset = NodeInfo.LockedJumpStartAnim.Get();
        const float ExpectedTime = ClampAnimationAssetTime(
            LockedAsset,
            NodeInfo.LockedJumpStartTime + FMath::Max(0.f, Context.GetDeltaTime()) * FMath::Max(0.f, TopPlayer.GetPlayRate()));
        const float TopTime = TopPlayer.GetAccumulatedTime();
        const bool bAssetChanged = TopAsset != LockedAsset;
        const bool bTimeRewound = TopTime + RemoteTransitionAllowedTimeSlack < ExpectedTime;
        const bool bTimeJumpedForward = TopTime > ExpectedTime + RemoteTransitionMaxForwardJump;

        bool bRestored = false;
        if (bAssetChanged || bTimeRewound || bTimeJumpedForward)
        {
            bRestored = RestoreBlendStackTopPlayer(Context, MotionMatchingNode, LockedAsset, ExpectedTime);
            if (bRestored)
            {
                NodeInfo.LockedJumpStartTime = ExpectedTime;
            }
        }
        else
        {
            NodeInfo.LockedJumpStartTime = ClampAnimationAssetTime(
                LockedAsset,
                FMath::Max(NodeInfo.LockedJumpStartTime, TopTime));
        }

        NodeInfo.bPostUpdateCollapsedTransitionStack =
            CollapseBlendStackToTopPlayer(MotionMatchingNode) || NodeInfo.bPostUpdateCollapsedTransitionStack;
        return bRestored;
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


    void ApplyJumpStartPredictionToTrajectory(
        FTransformTrajectory& Trajectory,
        const ABasePlayer& CharacterOwner,
        const ULocomotionAnimStateComponent& StateComponent)
    {
        const UCharacterMovementComponent* MovementComponent = CharacterOwner.GetCharacterMovement();
        if (!MovementComponent || !MovementComponent->IsFalling())
        {
            return;
        }

        const FVector LaunchDirection =
            CharacterOwner.GetActorForwardVector() * StateComponent.JumpStartMoveDirection.Y +
            CharacterOwner.GetActorRightVector() * StateComponent.JumpStartMoveDirection.X;
        const FVector HorizontalDirection = LaunchDirection.GetSafeNormal2D();
        const FVector Origin = CharacterOwner.GetActorLocation();
        const FVector Velocity = MovementComponent->Velocity;
        const float GravityZ = MovementComponent->GetGravityZ();

        for (FTransformTrajectorySample& Sample : Trajectory.Samples)
        {
            if (Sample.TimeInSeconds <= UE_KINDA_SMALL_NUMBER)
            {
                continue;
            }

            const float Time = Sample.TimeInSeconds;
            FTransform SampleTransform = Sample.GetTransform();
            FVector SampleLocation = Origin + HorizontalDirection * StateComponent.JumpStartGroundSpeed * Time;
            SampleLocation.Z = Origin.Z + Velocity.Z * Time + 0.5f * GravityZ * Time * Time;
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
                const bool bIsJumpStartPhase =
                    CurrentMotionState == ELocomotionState::InAir &&
                    ThreadSafeData.AirData.bIsJumping;
                const bool bIsProtectedOneShotState = bIsTransitionState || bIsJumpStartPhase;
                const bool bIsAirLoopPhase =
                    ThreadSafeData.AirData.bIsInAir &&
                    !ThreadSafeData.AirData.bIsJumping &&
                    !ThreadSafeData.AirData.bIsFallOffStart &&
                    !ThreadSafeData.LandingData.bIsLanding &&
                    !ThreadSafeData.LandingData.bLandingRequested;
                const bool bIsRemoteIdleHold =
                    bIsRemoteSimProxy &&
                    CurrentMotionState == ELocomotionState::Idle &&
                    !ThreadSafeData.InputData.bHasMoveInput &&
                    !bSearchResultDatabaseChanged &&
                    !bAppliedDatabaseChanged;
                if (bAppliedDatabaseChanged)
                {
                    // Jump start must replace a continuing ground pose on the
                    // very first airborne frame. General air loops retain
                    // continuity after their initial search.
                    const bool bInvalidateContinuingPose =
                        bIsJumpStartPhase ||
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
                else if (bIsProtectedOneShotState)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }
                else if (bIsFallOffStartPhase && !bSearchResultDatabaseChanged && !bAppliedDatabaseChanged)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }
                else if (bIsAirLoopPhase && !bSearchResultDatabaseChanged && !bAppliedDatabaseChanged)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }
                else if (bIsRemoteIdleHold)
                {
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
                }

                if (bIsJumpStartPhase || bIsFallOffStartPhase || bIsAirLoopPhase)
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
                        const bool bStableAirLoop =
                            !bSearchResultDatabaseChanged &&
                            !bAppliedDatabaseChanged;
                        SearchThrottleTime = bStableAirLoop
                            ? SuppressedSearchThrottleTime
                            : Info.DefaultSearchThrottleTime;
                    }
                    else
                    {
                        UMotionMatchingAnimInstance* MMAnim = Cast<UMotionMatchingAnimInstance>(AnimInstanceObj);
                        const ULocomotionAnimStateComponent* StateComp = MMAnim ? MMAnim->CachedLocomotionStateComponent.Get() : nullptr;
                        if (bIsJumpStartPhase || (StateComp && (StateComp->CurrentState == ELocomotionState::Start || StateComp->CurrentState == ELocomotionState::Stop || StateComp->CurrentState == ELocomotionState::Landing)))
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
    const bool bStabilizeJumpStart =
        UpdatedMotionState == ELocomotionState::InAir &&
        ThreadSafeData.AirData.bIsJumping;
    const bool bStabilizeRemoteTransition =
        UpdatedPawn &&
        UpdatedPawn->GetLocalRole() == ROLE_SimulatedProxy &&
        UpdatedMotionState == ELocomotionState::Landing;
    if (bStabilizeJumpStart || bStabilizeRemoteTransition)
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
                    Info.bPostUpdateRestoredTransitionStack = bStabilizeJumpStart
                        ? StabilizeJumpStartBlendStackPlayer(InContext, *MMNode, Info)
                        : StabilizeRemoteTransitionBlendStackPlayer(InContext, *MMNode, Info, UpdatedMotionState);
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
            Info.bHasJumpStartLock = false;
            Info.LockedJumpStartAnim.Reset();
            Info.LockedJumpStartTime = 0.f;
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
	const bool bHasAuthoredBowDrawPose =
		ThreadSafeData.BowData.bIsDrawing ||
		ThreadSafeData.BowData.bIsFullyDrawn ||
		ThreadSafeData.BowData.bIsReleasing;

	if (bSuppressAimOffsetWhileBowFullyDrawn && bHasAuthoredBowDrawPose)
	{
		return 0.f;
	}

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

    // Swimming visual state must remain current even when distant characters
    // skip a costly motion-matching search this frame.
    FSwimmingAnimationState CurrentSwimData;
    if (const USwimmingComponent* SwimmingComponent = CachedBasePlayer->GetSwimmingComponent())
    {
        CurrentSwimData = SwimmingComponent->GetAnimationState();
    }

    // A linked animation-layer instance receives the main instance's snapshot.
    // Keep using that explicit source even if its own NativeUpdate is evaluated
    // at a different point in the linked-layer update order.
    if (bHasLinkedSwimAnimationState && GetSkelMeshComponent() && GetSkelMeshComponent()->GetAnimInstance() != this)
    {
        CurrentSwimData = LinkedSwimAnimationState;
    }
    GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData = CurrentSwimData;
    PropagateSwimAnimationStateToLinkedInstances(CurrentSwimData);

    UpdateMovementDirection();
    CalculateAOValueAndEnableAO();

    // The master AnimBP owns direct Chooser playback. Linked layers share this
    // class, but must not independently advance/reselect the same one-shot.
    const bool bIsPrimaryAnimInstance = GetSkelMeshComponent() && GetSkelMeshComponent()->GetAnimInstance() == this;
    if (bIsPrimaryAnimInstance)
    {
        EvaluateStateControllerPresentationState();
    }

    // 최적화 틱 레이트에 맞추어 이번 프레임의 모션 매칭 평가 여부 결정
    if (!ShouldEvaluateMotionMatchingThisFrame(DeltaSeconds))
    {
        return;
    }

    // 1. C++ 직접 상태 분기 및 알맞은 PSD 할당 (Chooser Table 미사용)
    CurrentActivePoseSearchDatabase = nullptr;

    if (CachedLocomotionStateComponent)
    {
        const bool bSprinting = CachedLocomotionStateComponent->bIsSprinting;

        switch (CachedLocomotionStateComponent->CurrentState)
        {
        case ELocomotionState::Idle:
            CurrentActivePoseSearchDatabase = IdleDatabase;
            break;

        case ELocomotionState::Start:
            // Start is now a direct Chooser/Blend Stack one-shot. Keep MM on a
            // locomotion cycle solely as the null-result fallback.
            CurrentActivePoseSearchDatabase = bSprinting
                ? SprintLocomotionDatabase
                : LocomotionDatabase;
            break;
#if 0 // Legacy Start PSD routing. Retained temporarily for editor migration reference.
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
#endif

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
                    CurrentActivePoseSearchDatabase = LocomotionTransitionDatabase;
                }
                else
                {
                    CurrentActivePoseSearchDatabase = LocomotionDatabase;
                }
            }
            break;

        case ELocomotionState::Stop:
            // Stop is a direct Chooser one-shot. MM is only the fallback.
            CurrentActivePoseSearchDatabase = CachedLocomotionStateComponent->GroundSpeed > 10.0f
                ? (bSprinting ? SprintLocomotionDatabase : LocomotionDatabase)
                : IdleDatabase;
            break;

        case ELocomotionState::InAir:
            // JumpStart/FallOffStart are direct Chooser one-shots. Only the
            // sustained air loop belongs to Motion Matching.
            CurrentActivePoseSearchDatabase = InAirDatabase;
            break;

        case ELocomotionState::Landing:
            // Landing is a direct Chooser one-shot. Do not race it with a PSD.
            CurrentActivePoseSearchDatabase = CachedLocomotionStateComponent->bLandWasMoving
                ? (CachedLocomotionStateComponent->bLandWasSprinting ? SprintLocomotionDatabase : LocomotionDatabase)
                : IdleDatabase;
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
        const bool bIsJumpStartPhase =
            State == ELocomotionState::InAir &&
            CachedLocomotionStateComponent->bIsJumping;
        if (IsTransitionMotionMatchingState(State) || bIsJumpStartPhase)
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

    // Direct Chooser one-shots own completion through animation notifies and
    // component fallback timers. MM must not complete hidden fallback clips.
#if 0
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

#endif

    // 2. Pack data into thread-safe struct. State Controller evaluation runs
    // before this payload is rebuilt so it remains available even when Motion
    // Matching is throttled. Preserve that completed chooser contract instead
    // of overwriting it with the default-constructed StateController payload.
    FAnimThreadSafeData ThreadSafeData;
    ThreadSafeData.StateController = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController;

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

                    if (State == ELocomotionState::InAir && CachedLocomotionStateComponent->bIsJumping)
                    {
                        ApplyJumpStartPredictionToTrajectory(ThreadSafeData.MovementData.Trajectory, *CachedBasePlayer, *CachedLocomotionStateComponent);
                    }
                    else if (State == ELocomotionState::InAir && CachedLocomotionStateComponent->bIsFallOffStart)
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
    ThreadSafeData.AirData.bJumpStartWasMoving = CachedLocomotionStateComponent->bJumpStartWasMoving;
    ThreadSafeData.AirData.JumpStartGroundSpeed = CachedLocomotionStateComponent->JumpStartGroundSpeed;
    ThreadSafeData.AirData.JumpStartMoveDirection = CachedLocomotionStateComponent->JumpStartMoveDirection;

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
    }

    ThreadSafeData.WeaponUpperBodyData = FAnimWeaponUpperBodyData();
    ThreadSafeData.BowData = FAnimBowData();
    ThreadSafeData.SwimData = CurrentSwimData;

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
        const float Direction = bForceSprintWeaponUpperBodyDirectionForward && bSprinting
            ? 0.f
            : FRotator::NormalizeAxis(CachedLocomotionStateComponent->MovementDirection);

        ThreadSafeData.WeaponUpperBodyData.bHasWeaponEquipped = bHasWeaponEquipped;
        ThreadSafeData.WeaponUpperBodyData.EquippedWeaponTag = EquippedWeaponTag;
        ThreadSafeData.WeaponUpperBodyData.OverlayTag = OverlayTag;
        ThreadSafeData.WeaponUpperBodyData.OverlayIndex = EquipmentComponent->GetEquippedUpperBodyOverlayIndex();
        ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody = bEnableWeaponUpperBodyOverlay && bUseWeaponOverlay && bGroundedForOverlay;
        ThreadSafeData.WeaponUpperBodyData.UpperBodyAlpha = ThreadSafeData.WeaponUpperBodyData.bShouldOverrideUpperBody ? 1.f : 0.f;
        ThreadSafeData.WeaponUpperBodyData.GroundSpeed = GroundSpeed;
        ThreadSafeData.WeaponUpperBodyData.Direction = Direction;
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

        if (const ABowItem* EquippedBow = Cast<ABowItem>(CachedBasePlayer->EquippedItem))
        {
            if (const UBowComponent* BowComponent = EquippedBow->GetBowComponent())
            {
                ThreadSafeData.BowData.bIsAiming = BowComponent->IsAiming();
                ThreadSafeData.BowData.DrawAlpha = BowComponent->GetDrawAlpha();
                ThreadSafeData.BowData.bIsDrawing = ThreadSafeData.BowData.bIsAiming && ThreadSafeData.BowData.DrawAlpha > KINDA_SMALL_NUMBER;
                ThreadSafeData.BowData.bIsFullyDrawn = ThreadSafeData.BowData.DrawAlpha >= 1.f - KINDA_SMALL_NUMBER;

                FTransform StringIKTargetWorldTransform = FTransform::Identity;
                if (EquippedBow->GetStringIKTargetTransform(ThreadSafeData.BowData.DrawAlpha, StringIKTargetWorldTransform))
                {
                    if (const USkeletalMeshComponent* CharacterMesh = GetSkelMeshComponent())
                    {
                        ThreadSafeData.BowData.bHasStringIKTarget = true;
                        ThreadSafeData.BowData.StringIKTargetTransform =
                            StringIKTargetWorldTransform.GetRelativeTransform(CharacterMesh->GetComponentTransform());
                    }
                }
            }
        }

        if (const UAbilitySystemComponent* ASC = CachedBasePlayer->GetAbilitySystemComponent())
        {
            ThreadSafeData.BowData.bIsDrawing = ASC->HasMatchingGameplayTag(State_Bow_Drawing);
            // DrawAlpha reaches 1.0 in the same gameplay update that promotes the
            // ability to FullyDrawn. Keep that local visual transition immediate
            // instead of waiting for the gameplay-tag/proxy update on the next frame.
            ThreadSafeData.BowData.bIsFullyDrawn =
                ThreadSafeData.BowData.bIsFullyDrawn ||
                ASC->HasMatchingGameplayTag(State_Bow_FullyDrawn);
            ThreadSafeData.BowData.bIsReleasing = ASC->HasMatchingGameplayTag(State_Bow_Releasing);
        }

        // Preload the full-draw pose while the authored draw montage is still
        // visible. When that montage fades out, it therefore reveals the
        // matching pose rather than the regular bow upper-body blend space.
        ThreadSafeData.BowData.bShouldUseFullDrawPose =
            !ThreadSafeData.BowData.bIsReleasing &&
            (ThreadSafeData.BowData.bIsFullyDrawn ||
             (ThreadSafeData.BowData.bIsAiming &&
              ThreadSafeData.BowData.DrawAlpha >= FullDrawPosePreloadAlpha));

        ThreadSafeData.BowData.StringIKAlpha =
            bEnableBowStringHandIK &&
            ThreadSafeData.BowData.bHasStringIKTarget &&
            ThreadSafeData.BowData.bIsFullyDrawn &&
            !ThreadSafeData.BowData.bIsReleasing
                ? 1.f
                : 0.f;

		if (CachedBasePlayer->GetController())
		{
			ThreadSafeData.AimData.AimOffsetAlpha = CalculateAimOffsetAlpha(ThreadSafeData);
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

float UMotionMatchingAnimInstance::GetThreadSafeWeaponUpperBodyDirection() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.WeaponUpperBodyData.Direction;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsBowAiming() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bIsAiming;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsBowDrawing() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bIsDrawing;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsBowFullyDrawn() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bIsFullyDrawn;
}

bool UMotionMatchingAnimInstance::GetThreadSafeShouldUseBowFullDrawPose() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bShouldUseFullDrawPose;
}

float UMotionMatchingAnimInstance::GetThreadSafeBowHoldAimOffsetAlpha() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return bEnableBowHoldAimOffset &&
        ThreadSafeData.BowData.bIsFullyDrawn &&
        !ThreadSafeData.BowData.bIsReleasing
            ? BowHoldAimOffsetAlpha
            : 0.f;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsBowReleasing() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bIsReleasing;
}

float UMotionMatchingAnimInstance::GetThreadSafeBowDrawAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.DrawAlpha;
}

bool UMotionMatchingAnimInstance::GetThreadSafeHasBowStringIKTarget() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.bHasStringIKTarget;
}

float UMotionMatchingAnimInstance::GetThreadSafeBowStringIKAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.StringIKAlpha;
}

FTransform UMotionMatchingAnimInstance::GetThreadSafeBowStringIKTargetTransform() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.BowData.StringIKTargetTransform;
}

void UMotionMatchingAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();

    // Linked layers can share this class. Only the mesh's primary AnimInstance
    // owns the contact cache that drives Chooser selection.
    if (!GetSkelMeshComponent() || GetSkelMeshComponent()->GetAnimInstance() != this)
    {
        return;
    }

    float LeftContact = 0.0f;
    float RightContact = 0.0f;
    const bool bHasLeftContact = GetCurveValue(StateControllerLeftFootContactCurveName, LeftContact);
    const bool bHasRightContact = GetCurveValue(StateControllerRightFootContactCurveName, RightContact);
    bHasStateControllerFootContactCurves = bHasLeftContact && bHasRightContact;

    if (!bHasStateControllerFootContactCurves)
    {
        CachedStateControllerLeftFootContact = 0.0f;
        CachedStateControllerRightFootContact = 0.0f;
        return;
    }

    CachedStateControllerLeftFootContact = FMath::Clamp(LeftContact, 0.0f, 1.0f);
    CachedStateControllerRightFootContact = FMath::Clamp(RightContact, 0.0f, 1.0f);
    const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
    if (FMath::Abs(ContactDelta) >= StateControllerFootContactDifferenceThreshold)
    {
        // Lfoot/Rfoot transitions begin with the less planted (swinging) foot.
        StateControllerFootPhaseHistory = ContactDelta < 0.0f
            ? EStateControllerOneShotFoot::Left
            : EStateControllerOneShotFoot::Right;
        bHasStateControllerFootPhaseHistory = true;
    }
}

void UMotionMatchingAnimInstance::ReceiveLinkedSwimAnimationState(const FSwimmingAnimationState& InSwimState)
{
    LinkedSwimAnimationState = InSwimState;
    bHasLinkedSwimAnimationState = true;
    GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData = InSwimState;
}

void UMotionMatchingAnimInstance::PropagateSwimAnimationStateToLinkedInstances(const FSwimmingAnimationState& InSwimState)
{
    USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
    if (!MeshComponent || MeshComponent->GetAnimInstance() != this)
    {
        return;
    }

    if (UAnimInstance* LinkedInstance = GetLinkedAnimLayerInstanceByClass(UMotionMatchingAnimInstance::StaticClass(), true))
    {
        if (UMotionMatchingAnimInstance* LinkedMotionMatchingInstance = Cast<UMotionMatchingAnimInstance>(LinkedInstance))
        {
            LinkedMotionMatchingInstance->ReceiveLinkedSwimAnimationState(InSwimState);
        }
    }

}

bool UMotionMatchingAnimInstance::GetThreadSafeIsSwimming() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.bIsSwimming;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsUnderwater() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.bIsUnderwater;
}

bool UMotionMatchingAnimInstance::GetThreadSafeSwimDiveInputHeld() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.bDiveInputHeld;
}

bool UMotionMatchingAnimInstance::GetThreadSafeSwimAscendInputHeld() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.bAscendInputHeld;
}

ESwimDepthMode UMotionMatchingAnimInstance::GetThreadSafeSwimDepthMode() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.DepthMode;
}

float UMotionMatchingAnimInstance::GetThreadSafeSwimSpeed() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.HorizontalSpeed;
}

float UMotionMatchingAnimInstance::GetThreadSafeSwimVerticalSpeed() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.VerticalSpeed;
}

float UMotionMatchingAnimInstance::GetThreadSafeSwimDirection() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.SwimData.Direction;
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

void UMotionMatchingAnimInstance::EvaluateStateControllerPresentationState()
{
    if (!CachedLocomotionStateComponent) return;

    FAnimThreadSafeData& ThreadSafeData = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    EStateControllerPresentationState DesiredState = EStateControllerPresentationState::IdleLoop;

    // State Controller runs before NativeUpdateAnimation repacks the proxy for
    // Motion Matching.  It must therefore use the component's current
    // game-thread values here; reading ThreadSafeData would be one or more
    // updates behind.  That stale snapshot was able to miss the short Landing
    // window entirely and delayed Stop after input release.
    const bool bHasMoveInput = CachedLocomotionStateComponent->bHasMoveInput;
    const float GroundSpeed = CachedLocomotionStateComponent->GroundSpeed;
    const bool bInAir = CachedLocomotionStateComponent->bIsInAir;
    const bool bLanding = CachedLocomotionStateComponent->bIsLanding && CachedLocomotionStateComponent->bLandingRequested;
    const bool bShouldTurnInPlace = CachedLocomotionStateComponent->bShouldTurnInPlace;
    const bool bInPlaybackHold = (StateControllerPlaybackHoldElapsed < StateControllerPlaybackHoldDuration);

    // StartLanding deliberately keeps bIsInAir true until the landing pose is
    // released.  Landing must therefore take precedence over the air flag.
    if (bLanding)
    {
        DesiredState = EStateControllerPresentationState::TransitionToLand;
    }
    else if (bInAir)
    {
        DesiredState = EStateControllerPresentationState::TransitionToJump;
    }
    // The locomotion component's transitional phase is authoritative for a
    // direct one-shot.  In particular, Stop must pre-empt Start immediately;
    // deriving it only from decelerating velocity can leave Start held for one
    // or more updates and makes diagonal Stop selection appear intermittent.
    else if (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop)
    {
        DesiredState = EStateControllerPresentationState::TransitionToStop;
    }
    else if (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start)
    {
        DesiredState = bHasMoveInput
            ? EStateControllerPresentationState::TransitionToStart
            : EStateControllerPresentationState::TransitionToStop;
    }
    else if (bHasMoveInput || GroundSpeed > 10.0f)
    {
        const bool bIsPivoting = CachedLocomotionStateComponent->bSharpTurnRequested;
        if (bIsPivoting && (StateControllerPlaybackHoldState == EStateControllerPresentationState::LocomotionLoop || StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart))
        {
            DesiredState = EStateControllerPresentationState::TransitionToPivot;
        }
        else if (StateControllerPlaybackHoldState == EStateControllerPresentationState::IdleLoop ||
            StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace ||
            StateControllerPlaybackHoldState == EStateControllerPresentationState::None)
        {
            DesiredState = EStateControllerPresentationState::TransitionToStart;
        }
        else if (bInPlaybackHold && StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart)
        {
            DesiredState = EStateControllerPresentationState::TransitionToStart;
        }
        else
        {
            DesiredState = EStateControllerPresentationState::LocomotionLoop;
        }
    }
    else
    {
        if (bShouldTurnInPlace)
        {
            DesiredState = EStateControllerPresentationState::TurnInPlace;
        }
        else if (bInPlaybackHold && StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop)
        {
            DesiredState = EStateControllerPresentationState::TransitionToStop;
        }
        else if (GroundSpeed > 100.0f || (bInPlaybackHold && StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart))
        {
            DesiredState = EStateControllerPresentationState::TransitionToStop;
        }
        else
        {
            DesiredState = EStateControllerPresentationState::IdleLoop;
        }
    }

    // Keep the gameplay request separately for diagnostics. Land is held for
    // its authored playable length when it naturally continues to a loop.
    StateControllerRequestedPresentationState = DesiredState;
    const float LandCompletionTime = FMath::Max(
        StateControllerPlaybackHoldDuration - FMath::Max(StateControllerLandCompletionLeadTime, 0.0f),
        0.0f);
    const bool bReturningFromLandToGroundPresentation =
        DesiredState == EStateControllerPresentationState::LocomotionLoop ||
        DesiredState == EStateControllerPresentationState::IdleLoop ||
        DesiredState == EStateControllerPresentationState::TurnInPlace;
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand &&
        bReturningFromLandToGroundPresentation &&
        StateControllerSelectedAnimation &&
        StateControllerPlaybackHoldElapsed < LandCompletionTime)
    {
        DesiredState = EStateControllerPresentationState::TransitionToLand;
    }

    // Project_J treats a fresh move/facing intent as a presentation replacement,
    // not as a requirement to finish the previous transition.  Artistic is
    // always strafe, so capture the camera-relative input and control yaw at
    // Start entry and hand straight back to the locomotion PSD on a change.
    bStateControllerStartInputChanged = false;
    bStateControllerStartControlYawChanged = false;
    StateControllerStartInputDeltaDegrees = 0.0f;
    StateControllerStartControlYawDeltaDegrees = 0.0f;
    if (CachedLocomotionStateComponent &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart)
    {
        const FVector2D StartInput = StateControllerStartMoveInput.GetSafeNormal();
        const FVector2D CurrentInput = CachedLocomotionStateComponent->CachedMoveInput.GetSafeNormal();
        if (StartInput.IsNearlyZero() != CurrentInput.IsNearlyZero())
        {
            bStateControllerStartInputChanged = true;
        }
        else if (!StartInput.IsNearlyZero())
        {
            StateControllerStartInputDeltaDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
                FVector2D::DotProduct(StartInput, CurrentInput), -1.0f, 1.0f)));
            bStateControllerStartInputChanged =
                StateControllerStartInputDeltaDegrees >= StateControllerStartInputInterruptAngle;
        }

        if (CachedBasePlayer)
        {
            StateControllerStartControlYawDeltaDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(
                StateControllerStartControlYaw,
                CachedBasePlayer->GetControlRotation().Yaw));
            bStateControllerStartControlYawChanged =
                StateControllerStartControlYawDeltaDegrees >= StateControllerStartControlYawInterruptAngle;
        }

        if ((bStateControllerStartInputChanged || bStateControllerStartControlYawChanged) &&
            DesiredState == EStateControllerPresentationState::TransitionToStart)
        {
            const bool bContinueMoving = bHasMoveInput || GroundSpeed > 10.0f;
            DesiredState = bContinueMoving
                ? EStateControllerPresentationState::LocomotionLoop
                : EStateControllerPresentationState::TransitionToStop;
            StateControllerRequestedPresentationState = DesiredState;

            // A presentation-only exit is insufficient: the component would
            // still report Start on the next tick and re-enter its Chooser.
            // Commit the same semantic transition now, matching Project_J's
            // responsive Start exit policy.
            CachedLocomotionStateComponent->ForceStateTransition(
                bContinueMoving ? ELocomotionState::Locomotion : ELocomotionState::Stop);
        }
    }

    if (DesiredState == EStateControllerPresentationState::TransitionToLand)
    {
        if (!bHasStateControllerLandGaitLock)
        {
            const bool bMovingLand = CachedLocomotionStateComponent->bLandWasMoving;
            if (bMovingLand)
            {
                StateControllerLandGaitLock = CachedLocomotionStateComponent->bIsSprinting ? EGaitIntent::Sprint : EGaitIntent::Run;
            }
            else
            {
                StateControllerLandGaitLock = EGaitIntent::Walk;
            }
            bHasStateControllerLandGaitLock = true;
        }
    }
    else
    {
        bHasStateControllerLandGaitLock = false;
    }

    EvaluateStateControllerPlaybackHold(DesiredState);
}

void UMotionMatchingAnimInstance::EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState)
{
    FAnimThreadSafeData& ThreadSafeData = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    const bool bStateChanged = (DesiredState != StateControllerPlaybackHoldState);
    const EStateControllerPresentationState PreviousState = StateControllerPlaybackHoldState;

    bool bInterruptLandForMotionMatching = false;
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand &&
        DesiredState == EStateControllerPresentationState::LocomotionLoop)
    {
        const bool bWantsSprint = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting;
        const bool bLandWasSprinting = (StateControllerLandGaitLock == EGaitIntent::Sprint);

        if (!CachedLocomotionStateComponent || !CachedLocomotionStateComponent->bIsLanding ||
            (bLandWasSprinting && !bWantsSprint))
        {
            bInterruptLandForMotionMatching = true;
        }
    }

    if (bStateChanged || bInterruptLandForMotionMatching)
    {
        StateControllerPlaybackHoldState = DesiredState;
        StateControllerPlaybackHoldElapsed = 0.0f;

        if (DesiredState == EStateControllerPresentationState::TransitionToLand)
        {
            bHasStateControllerLandingDirectionLatch = false;
        }

        StateControllerPresentationState = StateControllerPlaybackHoldState;
        StateControllerMovementDirection = CurrentMovementDirection;
        StateControllerPreviousMovementDirection = MovementDirectionLastFrame;
        if (DesiredState == EStateControllerPresentationState::TransitionToStop &&
            PreviousState == EStateControllerPresentationState::TransitionToLand &&
            bHasStateControllerLandingDirectionLatch)
        {
            // Project_J preserves the last valid strafe sector when stopped.
            // Velocity is already near zero here, so recomputing it would first
            // choose Forward/Backward and visibly flash the wrong Stop clip.
            CurrentMovementDirection = StateControllerLandingDirectionLatch;
            StateControllerMovementDirection = StateControllerLandingDirectionLatch;
            StateControllerPreviousMovementDirection = StateControllerLandingDirectionLatch;
        }
        bStateControllerIsPivoting = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bSharpTurnRequested;
        if (bHasStateControllerLandGaitLock)
        {
            StateControllerGait = StateControllerLandGaitLock;
        }
        else
        {
            StateControllerGait = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting ? EGaitIntent::Sprint : EGaitIntent::Run;
        }

        const bool bEnteringOneShot =
            DesiredState == EStateControllerPresentationState::TransitionToStart ||
            DesiredState == EStateControllerPresentationState::TransitionToStop ||
            DesiredState == EStateControllerPresentationState::TransitionToJump ||
            DesiredState == EStateControllerPresentationState::TransitionToLand ||
            DesiredState == EStateControllerPresentationState::TransitionToPivot ||
            DesiredState == EStateControllerPresentationState::TurnInPlace;
        if (bEnteringOneShot)
        {
            // Starting from idle has no reliable stride history. Stop/land/jump
            // can use the previously valid phase when contacts are momentarily equal.
            const bool bAllowPhaseHistoryFallback =
                DesiredState != EStateControllerPresentationState::TransitionToStart &&
                PreviousState != EStateControllerPresentationState::IdleLoop;
            StateControllerOneShotFoot = ResolveStateControllerOneShotFoot(bAllowPhaseHistoryFallback);
        }

        if (DesiredState == EStateControllerPresentationState::TransitionToStart && CachedLocomotionStateComponent)
        {
            StateControllerStartMoveInput = CachedLocomotionStateComponent->CachedMoveInput;
            StateControllerStartControlYaw = CachedBasePlayer
                ? CachedBasePlayer->GetControlRotation().Yaw
                : 0.0f;
        }

        // Nested Choosers read these reflected properties directly. Publish the
        // current component values before evaluating the parent/child tables;
        // waiting until the end of this function makes the first Jump/Land row
        // evaluate with the previous state's conditions.
        StateControllerSpeed2D = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->GroundSpeed : 0.0f;
        StateControllerDesiredFacingDeltaYaw = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->DesiredFacingDeltaYaw : 0.0f;
        bStateControllerIsHeavyLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bUseHeavyLand;
        bStateControllerIsMovingLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bLandWasMoving;
        bStateControllerIsInAir = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsInAir;
        bStateControllerIsJumping = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsJumping;
        bStateControllerIsFallOff = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsFallOffStart;
        bStateControllerShouldTurnInPlace = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bShouldTurnInPlace;

        // Velocity is still near zero on the first Start frame.  For a
        // permanently-strafe character, select the Start row from the actual
        // local input instead of incorrectly reusing the previous Forward row.
        if (DesiredState == EStateControllerPresentationState::TransitionToStart && CachedLocomotionStateComponent &&
            !CachedLocomotionStateComponent->CachedMoveInput.IsNearlyZero())
        {
            StateControllerPreviousMovementDirection = CurrentMovementDirection;
            CurrentMovementDirection = ResolveStateControllerDirectionFromInput(CachedLocomotionStateComponent->CachedMoveInput);
            StateControllerMovementDirection = CurrentMovementDirection;
        }
        else if (DesiredState == EStateControllerPresentationState::TransitionToLand && CachedLocomotionStateComponent &&
            !CachedLocomotionStateComponent->LandMoveDirection.IsNearlyZero())
        {
            // Project_J derives a stable strafe sector from the landing
            // trajectory and preserves it when speed reaches zero. Artistic
            // already records that trajectory in LandMoveDirection at impact;
            // use this immutable snapshot rather than a later deceleration
            // velocity, which was collapsing diagonal landings into backward.
            StateControllerPreviousMovementDirection = CurrentMovementDirection;
            CurrentMovementDirection = ResolveStateControllerDirectionFromInput(
                CachedLocomotionStateComponent->LandMoveDirection);
            StateControllerMovementDirection = CurrentMovementDirection;
            StateControllerLandingDirectionLatch = CurrentMovementDirection;
            bHasStateControllerLandingDirectionLatch = true;
        }

        UChooserTable* TargetChooser = MainChooserTable;
        if (!TargetChooser)
        {
            switch (DesiredState)
            {
            case EStateControllerPresentationState::TransitionToStart:
                TargetChooser = StartChooserTable;
                break;
            case EStateControllerPresentationState::TransitionToStop:
                TargetChooser = StopChooserTable;
                break;
            case EStateControllerPresentationState::TransitionToJump:
                TargetChooser = InAirChooserTable;
                break;
            case EStateControllerPresentationState::TransitionToLand:
                TargetChooser = LandChooserTable;
                break;
            case EStateControllerPresentationState::TransitionToPivot:
                TargetChooser = PivotChooserTable;
                break;
            case EStateControllerPresentationState::TurnInPlace:
                TargetChooser = TurnInPlaceChooserTable;
                break;
            default:
                TargetChooser = nullptr;
                break;
            }
        }

        if (TargetChooser)
        {
            StateControllerLastChooserPath = GetNameSafe(TargetChooser);
            StateControllerLastChooserOutputTrace.Reset();
            FChooserEvaluationContext ChooserContext;
            ChooserContext.AddObjectParam(this);

            FChooserPlayerSettings PlayerSettings;
            ChooserContext.AddStructParam(PlayerSettings);

            FS_ChooserOutputs ChooserOutputs;
            ChooserContext.AddStructParam(ChooserOutputs);

            const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(TargetChooser);
            UObject* EvaluatedObject = ChooserObject.IsValid()
                ? UChooserFunctionLibrary::EvaluateObjectChooserBase(ChooserContext, ChooserObject, UObject::StaticClass())
                : nullptr;
            StateControllerLastChooserOutputTrace = FString::Printf(
                TEXT("%s(Start=%.3f, Blend=%.3f)"),
                *GetNameSafe(TargetChooser),
                ChooserOutputs.StartTime,
                ChooserOutputs.BlendTime);

            while (UChooserTable* SubChooserTable = Cast<UChooserTable>(EvaluatedObject))
            {
                StateControllerLastChooserPath += FString::Printf(TEXT(" -> %s"), *SubChooserTable->GetName());
                const FInstancedStruct SubChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(SubChooserTable);
                if (!SubChooserObject.IsValid())
                {
                    EvaluatedObject = nullptr;
                    break;
                }
                EvaluatedObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(ChooserContext, SubChooserObject, UObject::StaticClass());
                StateControllerLastChooserOutputTrace += FString::Printf(
                    TEXT(" -> %s(Start=%.3f, Blend=%.3f)"),
                    *SubChooserTable->GetName(),
                    ChooserOutputs.StartTime,
                    ChooserOutputs.BlendTime);
            }

            StateControllerSelectedAnimation = Cast<UAnimationAsset>(EvaluatedObject);
            if (!StateControllerSelectedAnimation)
            {
                StateControllerLastChooserPath += TEXT(" -> <No Animation Row>");
            }
            // Keep the entire output structure, exactly as Project_J does.
            // StartTime/BlendTime remain one atomic authored contract with the
            // chosen asset rather than unrelated transient float values.
            StateControllerSelectedAnimationOutput = ChooserOutputs;
            StateControllerSelectedAnimationBlendTime = StateControllerSelectedAnimationOutput.BlendTime > 0.0f
                ? StateControllerSelectedAnimationOutput.BlendTime
                : 0.2f;
            StateControllerSelectedAnimationStartTime = StateControllerSelectedAnimation
                ? FMath::Clamp(StateControllerSelectedAnimationOutput.StartTime, 0.0f, StateControllerSelectedAnimation->GetPlayLength())
                : 0.0f;
            bStateControllerSelectedAnimationShouldLoop = false;
            StateControllerPlaybackHoldDuration = StateControllerSelectedAnimation
                ? FMath::Max(StateControllerSelectedAnimation->GetPlayLength() - StateControllerSelectedAnimationStartTime, 0.0f)
                : 0.0f;

            // The state component initially installs a conservative safety
            // timer (0.8s). Replace it with the actual Chooser clip duration
            // so Start/Stop/Jump/Land are not force-completed mid-animation.
            if (StateControllerSelectedAnimation && CachedLocomotionStateComponent)
            {
                CachedLocomotionStateComponent->RefreshOneShotFallbackTimer(StateControllerPlaybackHoldDuration);
            }
            ++StateControllerSelectionRevision;

        }
        else
        {
            StateControllerLastChooserPath = TEXT("<No Chooser Assigned>");
            StateControllerLastChooserOutputTrace = TEXT("<No Chooser Assigned>");
            StateControllerSelectedAnimation = nullptr;
            StateControllerSelectedAnimationOutput = FS_ChooserOutputs();
            StateControllerPlaybackHoldDuration = 0.0f;
            StateControllerSelectedAnimationStartTime = 0.0f;
            bStateControllerSelectedAnimationShouldLoop = false;
            ++StateControllerSelectionRevision;
        }
    }
    else
    {
        StateControllerPlaybackHoldElapsed += DeltaTime;
    }

    ThreadSafeData.StateController.PresentationState = StateControllerPlaybackHoldState;
    // Do not repack raw movement direction here.  A direct Stop can have a
    // deliberately latched Land direction which is different from zero-speed
    // velocity's fallback sector.
    ThreadSafeData.StateController.MovementDirection = StateControllerMovementDirection;
    ThreadSafeData.StateController.PreviousMovementDirection = StateControllerPreviousMovementDirection;
    ThreadSafeData.StateController.SelectedAnimation = StateControllerSelectedAnimation;
    ThreadSafeData.StateController.SelectedAnimationOutput = StateControllerSelectedAnimationOutput;
    ThreadSafeData.StateController.SelectedAnimationBlendTime = StateControllerSelectedAnimationBlendTime;
    ThreadSafeData.StateController.SelectedAnimationStartTime = StateControllerSelectedAnimationStartTime;
    ThreadSafeData.StateController.bSelectedAnimationShouldLoop = bStateControllerSelectedAnimationShouldLoop;
    ThreadSafeData.StateController.bHasSelectedAnimation = StateControllerSelectedAnimation != nullptr;
    ThreadSafeData.StateController.SelectionRevision = StateControllerSelectionRevision;
    ThreadSafeData.StateController.bShouldOverrideMotionMatching =
        ThreadSafeData.StateController.bHasSelectedAnimation &&
        StateControllerPlaybackHoldState != EStateControllerPresentationState::LocomotionLoop &&
        StateControllerPlaybackHoldState != EStateControllerPresentationState::IdleLoop;
    ThreadSafeData.StateController.TurnInPlaceSteeringAlpha = 1.0f;

    if (CachedLocomotionStateComponent)
    {
        ThreadSafeData.StateController.TurnInPlaceRootYawDelta = CachedLocomotionStateComponent->TurnInPlaceRootYawDelta;
    }

    StateControllerPresentationState = StateControllerPlaybackHoldState;
    const bool bHoldingLandStopDirection =
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop &&
        bHasStateControllerLandingDirectionLatch;
    if (!bHoldingLandStopDirection)
    {
        StateControllerMovementDirection = CurrentMovementDirection;
        StateControllerPreviousMovementDirection = MovementDirectionLastFrame;
    }
    bStateControllerIsPivoting = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bSharpTurnRequested;
    if (bHasStateControllerLandGaitLock)
    {
        StateControllerGait = StateControllerLandGaitLock;
    }
    else
    {
        StateControllerGait = (CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting
            ? EGaitIntent::Sprint
            : (ThreadSafeData.MovementData.Velocity.Size2D() > 10.0f || ThreadSafeData.InputData.bHasMoveInput ? EGaitIntent::Run : EGaitIntent::Walk));
    }
    StateControllerSpeed2D = ThreadSafeData.MovementData.Velocity.Size2D();
    StateControllerDesiredFacingDeltaYaw = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->DesiredFacingDeltaYaw : 0.0f;
    bStateControllerIsHeavyLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bUseHeavyLand;
    bStateControllerIsMovingLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bLandWasMoving;
    bStateControllerIsInAir = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsInAir;
    bStateControllerIsJumping = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsJumping;
    bStateControllerIsFallOff = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsFallOffStart;
    bStateControllerShouldTurnInPlace = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->bShouldTurnInPlace : false;

    EmitStateControllerDebugTrace(ThreadSafeData);
}

void UMotionMatchingAnimInstance::EmitStateControllerDebugTrace(const FAnimThreadSafeData& ThreadSafeData)
{
    if (CVarAnimStateControllerDebug.GetValueOnGameThread() <= 0 || !CachedBasePlayer || !CachedLocomotionStateComponent ||
        !GetSkelMeshComponent() || GetSkelMeshComponent()->GetAnimInstance() != this)
    {
        return;
    }

    const FAnimStateControllerThreadSafeData& StateController = ThreadSafeData.StateController;
    const ELocomotionState LocomotionState = CachedLocomotionStateComponent->CurrentState;
    const bool bPresentationChanged = StateController.PresentationState != LastStateControllerDebugPresentation;
    const bool bSelectionChanged = StateController.SelectionRevision != LastStateControllerDebugSelectionRevision;
    const bool bComponentEventChanged = CachedLocomotionStateComponent->GetStateControllerDebugEventRevision() != LastStateControllerDebugComponentEventRevision;
    const bool bDirectOneShot = StateController.bShouldOverrideMotionMatching ||
        LocomotionState == ELocomotionState::Start ||
        LocomotionState == ELocomotionState::Stop ||
        LocomotionState == ELocomotionState::TurnInPlace ||
        LocomotionState == ELocomotionState::Landing ||
        LocomotionState == ELocomotionState::InAir;
    const bool bTurnInPlace =
        StateController.PresentationState == EStateControllerPresentationState::TurnInPlace ||
        CachedLocomotionStateComponent->bShouldTurnInPlace;
    const bool bLandDiagnostic =
        LocomotionState == ELocomotionState::Landing ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToLand ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToLand;
    const bool bStartDiagnostic =
        LocomotionState == ELocomotionState::Start ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToStart ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToStart;
    const bool bStopDiagnostic =
        LocomotionState == ELocomotionState::Stop ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToStop ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToStop;
    const bool bPivotDiagnostic =
        CachedLocomotionStateComponent->bSharpTurnRequested ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToPivot ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToPivot ||
        (CachedLocomotionStateComponent->bHasMoveInput &&
            FMath::Abs(CachedLocomotionStateComponent->MoveInputTurnAngle) >= CachedLocomotionStateComponent->MoveInputTurnDeadZoneAngle);
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const bool bTurnInPlaceSampleDue = bTurnInPlace && Now >= NextStateControllerTurnInPlaceDebugTime;
    const bool bOneShotSampleDue = StateController.bShouldOverrideMotionMatching &&
        StateController.bHasSelectedAnimation && Now >= NextStateControllerOneShotDebugTime;
    const bool bPivotSampleDue = bPivotDiagnostic && Now >= NextStateControllerPivotDebugTime;

    if (!((bPresentationChanged || bSelectionChanged) && bDirectOneShot) && !bTurnInPlaceSampleDue && !bOneShotSampleDue && !bPivotSampleDue && !bComponentEventChanged)
    {
        return;
    }

    // Keep a.StateControllerDebug focused on the active land-direction issue.
    // Stop/Start traces are intentionally suppressed; their revision state is
    // still advanced so they cannot surface later as stale changes.
    if (!bLandDiagnostic && !bStartDiagnostic && !bStopDiagnostic && !bTurnInPlace && !bPivotDiagnostic)
    {
        LastStateControllerDebugPresentation = StateController.PresentationState;
        LastStateControllerDebugSelectionRevision = StateController.SelectionRevision;
        LastStateControllerDebugComponentEventRevision = CachedLocomotionStateComponent->GetStateControllerDebugEventRevision();
        NextStateControllerOneShotDebugTime = Now;
        return;
    }

    const UEnum* LocomotionEnum = StaticEnum<ELocomotionState>();
    const UEnum* PresentationEnum = StaticEnum<EStateControllerPresentationState>();
    const UEnum* DirectionEnum = StaticEnum<EMovementDirection>();
    const UEnum* GaitEnum = StaticEnum<EGaitIntent>();
    const FString LocomotionName = LocomotionEnum
        ? LocomotionEnum->GetNameStringByValue(static_cast<int64>(LocomotionState))
        : FString::FromInt(static_cast<int32>(LocomotionState));
    const FString PresentationName = PresentationEnum
        ? PresentationEnum->GetNameStringByValue(static_cast<int64>(StateController.PresentationState))
        : FString::FromInt(static_cast<int32>(StateController.PresentationState));
    const FString RequestedPresentationName = PresentationEnum
        ? PresentationEnum->GetNameStringByValue(static_cast<int64>(StateControllerRequestedPresentationState))
        : FString::FromInt(static_cast<int32>(StateControllerRequestedPresentationState));
    const FString DirectionName = DirectionEnum
        ? DirectionEnum->GetNameStringByValue(static_cast<int64>(StateController.MovementDirection))
        : FString::FromInt(static_cast<int32>(StateController.MovementDirection));
    const FString PreviousDirectionName = DirectionEnum
        ? DirectionEnum->GetNameStringByValue(static_cast<int64>(StateController.PreviousMovementDirection))
        : FString::FromInt(static_cast<int32>(StateController.PreviousMovementDirection));
    const FString LandingLatchName = bHasStateControllerLandingDirectionLatch
        ? (DirectionEnum
            ? DirectionEnum->GetNameStringByValue(static_cast<int64>(StateControllerLandingDirectionLatch))
            : FString::FromInt(static_cast<int32>(StateControllerLandingDirectionLatch)))
        : TEXT("None");
    const FString GaitName = GaitEnum
        ? GaitEnum->GetNameStringByValue(static_cast<int64>(StateControllerGait))
        : FString::FromInt(static_cast<int32>(StateControllerGait));
    const TCHAR* Reason = bSelectionChanged ? TEXT("Selection")
        : (bPresentationChanged ? TEXT("Presentation")
            : (bPivotSampleDue ? TEXT("PivotSample")
                : (bTurnInPlaceSampleDue ? TEXT("TIPSample") : TEXT("HoldSample"))));

    UE_LOG(LogMotionMatchingCapture, Display,
        TEXT("[SC_TRACE] Reason=%s Pawn=%s Locomotion=%s Requested=%s Presentation=%s Rev=%d OverrideMM=%d Asset=%s Start=%.3f Length=%.3f Elapsed=%.3f Blend=%.3f Loop=%d PSD=%s Input=(R=%.2f,F=%.2f) Speed=%.1f Direction=%s PrevDirection=%s LandStopLatch=%s Pivot=%d TurnAngle=%.1f PivotThreshold=%.1f PivotMinSpeed=%.1f RedirectThreshold=%.1f StartInputChanged=%d(%.1f/%.1f) StartYawChanged=%d(%.1f/%.1f) LandDir=(R=%.2f,F=%.2f) LandDirSource=%s LandDiagonal=%d EffectiveLandMin=%.2f LandCompletionLead=%.2f Gait=%s Foot=%s Jump=%d FallOff=%d Landing=%d HeavyLand=%d MovingLand=%d MovingLandSprint=%d LandingFromFallOff=%d LastFallSpeed=%.1f LandTime=%.3f Chooser=%s Outputs=%s"),
        Reason,
        *CachedBasePlayer->GetName(),
        *LocomotionName,
        *RequestedPresentationName,
        *PresentationName,
        StateController.SelectionRevision,
        StateController.bShouldOverrideMotionMatching ? 1 : 0,
        *GetNameSafe(StateController.SelectedAnimation),
        StateController.SelectedAnimationStartTime,
        StateControllerPlaybackHoldDuration,
        StateControllerPlaybackHoldElapsed,
        StateController.SelectedAnimationBlendTime,
        StateController.bSelectedAnimationShouldLoop ? 1 : 0,
        *GetNameSafe(CurrentActivePoseSearchDatabase),
        CachedLocomotionStateComponent->CachedMoveInput.X,
        CachedLocomotionStateComponent->CachedMoveInput.Y,
        CachedLocomotionStateComponent->GroundSpeed,
        *DirectionName,
        *PreviousDirectionName,
        *LandingLatchName,
        CachedLocomotionStateComponent->bSharpTurnRequested ? 1 : 0,
        CachedLocomotionStateComponent->MoveInputTurnAngle,
        CachedLocomotionStateComponent->PivotAngleThreshold,
        CachedLocomotionStateComponent->PivotMinSpeed,
        CachedLocomotionStateComponent->SharpTurnAngleThreshold,
        bStateControllerStartInputChanged ? 1 : 0,
        StateControllerStartInputDeltaDegrees,
        StateControllerStartInputInterruptAngle,
        bStateControllerStartControlYawChanged ? 1 : 0,
        StateControllerStartControlYawDeltaDegrees,
        StateControllerStartControlYawInterruptAngle,
        CachedLocomotionStateComponent->LandMoveDirection.X,
        CachedLocomotionStateComponent->LandMoveDirection.Y,
        CachedLocomotionStateComponent->bLandDirectionFromVelocity ? TEXT("ImpactVelocity") : TEXT("InputFallback"),
        CachedLocomotionStateComponent->GetStateControllerDebugIsDiagonalLanding() ? 1 : 0,
        CachedLocomotionStateComponent->GetStateControllerDebugEffectiveMinimumLandingDuration(),
        StateControllerLandCompletionLeadTime,
        *GaitName,
        StateControllerOneShotFoot == EStateControllerOneShotFoot::Left ? TEXT("Left") : TEXT("Right"),
        CachedLocomotionStateComponent->bIsJumping ? 1 : 0,
        CachedLocomotionStateComponent->bIsFallOffStart ? 1 : 0,
        CachedLocomotionStateComponent->bIsLanding ? 1 : 0,
        CachedLocomotionStateComponent->bUseHeavyLand ? 1 : 0,
        CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
        CachedLocomotionStateComponent->bLandWasSprinting ? 1 : 0,
        CachedLocomotionStateComponent->bLandingFromFallOff ? 1 : 0,
        CachedLocomotionStateComponent->LastFallSpeed,
        CachedLocomotionStateComponent->LandingElapsedTime,
        *StateControllerLastChooserPath,
        *StateControllerLastChooserOutputTrace);

    if (bComponentEventChanged)
    {
        UE_LOG(LogMotionMatchingCapture, Display,
            TEXT("[SC_COMPONENT] Pawn=%s EventRev=%d Event=%s Current=%s Air=%d PhysicalAir=%d Landing=%d Requested=%d LandMoving=%d LandInputSeen=%d HasInput=%d PrevInput=%d Input=(%.2f,%.2f) LandTime=%.3f MinLand=%.3f FallSpeed=%.1f"),
            *CachedBasePlayer->GetName(),
            CachedLocomotionStateComponent->GetStateControllerDebugEventRevision(),
            *CachedLocomotionStateComponent->GetStateControllerDebugLastEvent(),
            *LocomotionName,
            CachedLocomotionStateComponent->bIsInAir ? 1 : 0,
            CachedLocomotionStateComponent->bIsPhysicallyInAir ? 1 : 0,
            CachedLocomotionStateComponent->bIsLanding ? 1 : 0,
            CachedLocomotionStateComponent->bLandingRequested ? 1 : 0,
            CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
            CachedLocomotionStateComponent->bLandingReceivedMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->bPrevHasMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->CachedMoveInput.X,
            CachedLocomotionStateComponent->CachedMoveInput.Y,
            CachedLocomotionStateComponent->LandingElapsedTime,
            CachedLocomotionStateComponent->MinimumLandingDuration,
            CachedLocomotionStateComponent->LastFallSpeed);
    }

    if (bTurnInPlace)
    {
        const float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
        const float ControlYaw = CachedBasePlayer->GetControlRotation().Yaw;
        const float ControlMinusActorYaw = FMath::FindDeltaAngleDegrees(ActorYaw, ControlYaw);
        const float ActorYawDelta = bHasStateControllerDebugActorYaw
            ? FMath::FindDeltaAngleDegrees(LastStateControllerDebugActorYaw, ActorYaw)
            : 0.0f;
        const UCharacterMovementComponent* MovementComponent = CachedBasePlayer->GetCharacterMovement();

        UE_LOG(LogMotionMatchingCapture, Display,
            TEXT("[SC_TIP] Pawn=%s ActorYaw=%.2f DeltaActorYaw=%.2f ControlYaw=%.2f ControlMinusActor=%.2f DesiredYaw=%.2f RootYawApplied=%.2f ShouldTIP=%d MoveInput=%d Speed=%.1f OrientToMove=%d UseControllerDesired=%d"),
            *CachedBasePlayer->GetName(),
            ActorYaw,
            ActorYawDelta,
            ControlYaw,
            ControlMinusActorYaw,
            CachedLocomotionStateComponent->DesiredFacingDeltaYaw,
            CachedLocomotionStateComponent->TurnInPlaceRootYawDelta,
            CachedLocomotionStateComponent->bShouldTurnInPlace ? 1 : 0,
            CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->GroundSpeed,
            MovementComponent && MovementComponent->bOrientRotationToMovement ? 1 : 0,
            MovementComponent && MovementComponent->bUseControllerDesiredRotation ? 1 : 0);

        LastStateControllerDebugActorYaw = ActorYaw;
        bHasStateControllerDebugActorYaw = true;
        NextStateControllerTurnInPlaceDebugTime = Now + 0.25;
    }
    else
    {
        bHasStateControllerDebugActorYaw = false;
        NextStateControllerTurnInPlaceDebugTime = Now;
    }

    NextStateControllerOneShotDebugTime = StateController.bShouldOverrideMotionMatching && StateController.bHasSelectedAnimation
        ? Now + 0.5
        : Now;
    NextStateControllerPivotDebugTime = bPivotDiagnostic ? Now + 0.25 : Now;

    LastStateControllerDebugPresentation = StateController.PresentationState;
    LastStateControllerDebugSelectionRevision = StateController.SelectionRevision;
    LastStateControllerDebugComponentEventRevision = CachedLocomotionStateComponent->GetStateControllerDebugEventRevision();
}

EStateControllerOneShotFoot UMotionMatchingAnimInstance::ResolveStateControllerOneShotFoot(const bool bAllowPhaseHistoryFallback) const
{
    if (bHasStateControllerFootContactCurves)
    {
        const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
        if (FMath::Abs(ContactDelta) >= StateControllerFootContactDifferenceThreshold)
        {
            return ContactDelta < 0.0f
                ? EStateControllerOneShotFoot::Left
                : EStateControllerOneShotFoot::Right;
        }
    }

    if (bAllowPhaseHistoryFallback && bHasStateControllerFootPhaseHistory)
    {
        return StateControllerFootPhaseHistory;
    }

    return StateControllerNoPhaseFootFallback;
}

void UMotionMatchingAnimInstance::UpdateMovementDirection()
{
    FAnimThreadSafeData& ThreadSafeData = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    MovementDirectionLastFrame = CurrentMovementDirection;

    // NativeUpdate runs before the current frame is packed into ThreadSafeData.
    // Use the component's fresh velocity here so Stop captures the direction it
    // was actually travelling (especially diagonals), rather than a stale pose
    // search snapshot from the prior update.
    const FVector CurrentVelocity = CachedLocomotionStateComponent
        ? CachedLocomotionStateComponent->Velocity
        : ThreadSafeData.MovementData.Velocity;
    const FVector CurrentAcceleration = CachedLocomotionStateComponent
        ? CachedLocomotionStateComponent->Acceleration
        : ThreadSafeData.MovementData.Acceleration;
    const bool bHasMoveInput = CachedLocomotionStateComponent
        ? CachedLocomotionStateComponent->bHasMoveInput
        : ThreadSafeData.InputData.bHasMoveInput;
    const bool bMoving = bHasMoveInput || CurrentVelocity.Size2D() > 10.0f;
    if (!bMoving)
    {
        return;
    }

    float Direction = 0.0f;
    FVector VelocityDir = FVector::ZeroVector;
    if (CachedBasePlayer)
    {
        FRotator ActorRotation = CachedBasePlayer->GetActorRotation();
        VelocityDir = CurrentVelocity.GetSafeNormal2D();
        if (!VelocityDir.IsNearlyZero())
        {
            Direction = FRotator::NormalizeAxis(VelocityDir.Rotation().Yaw - ActorRotation.Yaw);
        }
    }

    FVector AccelDir = CurrentAcceleration.GetSafeNormal2D();
    float TrajectoryTurnAngle = 0.0f;
    if (!VelocityDir.IsNearlyZero() && !AccelDir.IsNearlyZero())
    {
        TrajectoryTurnAngle = FRotator::NormalizeAxis(AccelDir.Rotation().Yaw - VelocityDir.Rotation().Yaw);
    }

    const bool bIsPivoting = (FMath::Abs(TrajectoryTurnAngle) >= 30.0f);
    ThreadSafeData.InputData.bSharpTurnRequested = bIsPivoting;

    if (Direction >= -22.5f && Direction <= 22.5f)
    {
        CurrentMovementDirection = EMovementDirection::Forward;
    }
    else if (Direction > 22.5f && Direction <= 67.5f)
    {
        CurrentMovementDirection = EMovementDirection::ForwardRight;
    }
    else if (Direction > 67.5f && Direction <= 112.5f)
    {
        CurrentMovementDirection = EMovementDirection::Right;
    }
    else if (Direction > 112.5f && Direction <= 157.5f)
    {
        CurrentMovementDirection = EMovementDirection::BackwardRight;
    }
    else if (Direction < -22.5f && Direction >= -67.5f)
    {
        CurrentMovementDirection = EMovementDirection::ForwardLeft;
    }
    else if (Direction < -67.5f && Direction >= -112.5f)
    {
        CurrentMovementDirection = EMovementDirection::Left;
    }
    else if (Direction < -112.5f && Direction >= -157.5f)
    {
        CurrentMovementDirection = EMovementDirection::BackwardLeft;
    }
    else
    {
        CurrentMovementDirection = EMovementDirection::Backward;
    }
}

void UMotionMatchingAnimInstance::CalculateAOValueAndEnableAO()
{
    if (!CachedBasePlayer)
    {
        return;
    }

    FAnimThreadSafeData& ThreadSafeData = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    FRotator ControlRotation = CachedBasePlayer->GetControlRotation();
    FRotator ActorRotation = CachedBasePlayer->GetActorRotation();
    FRotator DeltaRot = (ControlRotation - ActorRotation).GetNormalized();

    ThreadSafeData.AimData.AimYaw = DeltaRot.Yaw;
    ThreadSafeData.AimData.AimPitch = DeltaRot.Pitch;

    const float AbsYaw = FMath::Abs(DeltaRot.Yaw);
    const bool bMoving = ThreadSafeData.InputData.bHasMoveInput || ThreadSafeData.MovementData.Velocity.Size2D() > 10.0f;
    const float YawThreshold = bMoving ? 180.0f : 115.0f;

    bool bEnableAO = (AbsYaw <= YawThreshold);

    if (GetSlotMontageLocalWeight(FName(TEXT("DefaultSlot"))) >= 0.5f)
    {
        bEnableAO = false;
    }

    ThreadSafeData.StateController.bEnableAO = bEnableAO;
    ThreadSafeData.StateController.AOValue = FVector2D(DeltaRot.Yaw, DeltaRot.Pitch);
}

EStateControllerPresentationState UMotionMatchingAnimInstance::GetThreadSafeStateControllerPresentationState() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.PresentationState;
}

EMovementDirection UMotionMatchingAnimInstance::GetThreadSafeStateControllerMovementDirection() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.MovementDirection;
}

UAnimationAsset* UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectedAnimation() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.SelectedAnimation;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectedAnimationBlendTime() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.SelectedAnimationBlendTime;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectedAnimationStartTime() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.SelectedAnimationStartTime;
}

bool UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectedAnimationShouldLoop() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.bSelectedAnimationShouldLoop;
}

bool UMotionMatchingAnimInstance::GetThreadSafeStateControllerHasSelectedAnimation() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.bHasSelectedAnimation;
}

int32 UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectionRevision() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.SelectionRevision;
}

bool UMotionMatchingAnimInstance::GetThreadSafeShouldOverrideMotionMatching() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.bShouldOverrideMotionMatching;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.TurnInPlaceSteeringAlpha;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerCombatStateOrientationWarpingAngle() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    const FVector VelocityDir = ThreadSafeData.MovementData.Velocity.GetSafeNormal2D();
    if (!VelocityDir.IsNearlyZero() && CachedBasePlayer)
    {
        const FRotator ActorRot = CachedBasePlayer->GetActorRotation();
        return FRotator::NormalizeAxis(VelocityDir.Rotation().Yaw - ActorRot.Yaw);
    }
    return 0.0f;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerCombatStateOrientationWarpingAlpha() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return (ThreadSafeData.MovementData.Velocity.Size2D() > 10.0f || ThreadSafeData.InputData.bHasMoveInput) ? 1.0f : 0.0f;
}

FRotator UMotionMatchingAnimInstance::GetThreadSafeStateControllerDesiredFacingRotator() const
{
    if (CachedLocomotionStateComponent)
    {
        return FRotator(0.0f, CachedLocomotionStateComponent->DesiredFacingDeltaYaw, 0.0f);
    }
    return FRotator::ZeroRotator;
}

EOffsetRootBoneMode UMotionMatchingAnimInstance::GetThreadSafeOffsetRootRotationMode() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    if (ThreadSafeData.AirData.bIsInAir)
    {
        return EOffsetRootBoneMode::Release;
    }

    // Artistic is always Strafe. Keep the visual mesh centered during normal
    // locomotion and permit root rotation interpolation only for authored TIP.
    return ThreadSafeData.StateController.PresentationState == EStateControllerPresentationState::TurnInPlace
        ? EOffsetRootBoneMode::Interpolate
        : EOffsetRootBoneMode::Release;
}

EOffsetRootBoneMode UMotionMatchingAnimInstance::GetThreadSafeOffsetRootTranslationMode() const
{
    // Translation offsets make the visible mesh drift from the gameplay
    // capsule. Artistic's strafe locomotion deliberately keeps that offset at 0.
    return EOffsetRootBoneMode::Release;
}

float UMotionMatchingAnimInstance::GetThreadSafeOffsetRootTranslationHalfLife() const
{
    return 0.1f;
}

float UMotionMatchingAnimInstance::GetThreadSafeOffsetRootTranslationRadius() const
{
    return 30.0f;
}

FVector2D UMotionMatchingAnimInstance::GetThreadSafeAOValue() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.AOValue;
}

bool UMotionMatchingAnimInstance::GetThreadSafeEnableAO() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.bEnableAO;
}

EGaitIntent UMotionMatchingAnimInstance::GetThreadSafeGait() const
{
    if (bHasStateControllerLandGaitLock)
    {
        return StateControllerLandGaitLock;
    }
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    if (ThreadSafeData.GroundData.bStartRequested)
    {
        return EGaitIntent::Sprint;
    }
    const float GroundSpeed = ThreadSafeData.MovementData.Velocity.Size2D();
    if (GroundSpeed > 10.0f || ThreadSafeData.InputData.bHasMoveInput)
    {
        return EGaitIntent::Run;
    }
    return EGaitIntent::Walk;
}

float UMotionMatchingAnimInstance::GetThreadSafeSpeed2D() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.MovementData.Velocity.Size2D();
}

float UMotionMatchingAnimInstance::GetThreadSafeDesiredFacingDeltaYaw() const
{
    if (CachedLocomotionStateComponent)
    {
        return CachedLocomotionStateComponent->DesiredFacingDeltaYaw;
    }
    return 0.0f;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsHeavyLand() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.LandingData.bUseHeavyLand;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsMovingLand() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.InputData.bHasMoveInput || ThreadSafeData.MovementData.Velocity.Size2D() > 10.0f;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsInAir() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.AirData.bIsInAir;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsJumping() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.AirData.bIsJumping;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsFallOff() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.AirData.bIsFallOffStart;
}

EMovementDirection UMotionMatchingAnimInstance::GetThreadSafeStateControllerPreviousMovementDirection() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.PreviousMovementDirection;
}

bool UMotionMatchingAnimInstance::GetThreadSafeIsPivoting() const
{
    const FAnimThreadSafeData& ThreadSafeData = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    return ThreadSafeData.InputData.bSharpTurnRequested;
}

bool UMotionMatchingAnimInstance::GetThreadSafeShouldTurnInPlace() const
{
    if (CachedLocomotionStateComponent)
    {
        return CachedLocomotionStateComponent->bShouldTurnInPlace;
    }
    return false;
}
