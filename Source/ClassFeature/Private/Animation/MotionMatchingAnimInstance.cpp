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
#include "Components/CapsuleComponent.h"
#include "CollisionChannels.h"
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
#include "Animation/AnimBlueprintGeneratedClass.h"
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

static TAutoConsoleVariable<int32> CVarStopDebug(
    TEXT("a.StopDebug"),
    0,
    TEXT("Ground Stop presentation diagnostics. 0: Disabled, 1: request/selection/consume events."),
    ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarStartDebug(
    TEXT("a.StartDebug"),
    0,
    TEXT("Ground Start presentation diagnostics. 0: Disabled, 1: Start selection and bypass events."),
    ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarMotionMatchingDebugLogging(
    TEXT("p.MMDebugging"),
    0,
    TEXT("Motion Matching diagnostics. 0: Disabled, 1: Transition/search events, 2: Verbose frame/node/stack dumps"),
    ECVF_Default
);

static TAutoConsoleVariable<int32> CVarStrafeMotionMatchingDebug(
    TEXT("a.StrafeMMDebug"),
    0,
    TEXT("Moving-Strafe Pose Search diagnostics. 0: Disabled, 1: guaranteed game-thread heartbeat plus reselect events, 2: also 0.25s anim-thread selection samples."),
    ECVF_Cheat
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

    bool CollapseBlendStackToDominantPlayer(FAnimNode_MotionMatching& MotionMatchingNode);

    float NormalizeAnimationAssetTime(const UAnimationAsset* AnimationAsset, float Time, bool bLooping)
    {
        if (!bLooping || !AnimationAsset)
        {
            return ClampAnimationAssetTime(AnimationAsset, Time);
        }

        const float PlayLength = AnimationAsset->GetPlayLength();
        if (PlayLength <= UE_SMALL_NUMBER)
        {
            return 0.f;
        }

        // FAnimExtractContext expects the incoming position to be within one
        // asset cycle.  Blend Stack's accumulated clock can survive a graph
        // hand-off, so a Fall loop may otherwise enter root-motion extraction
        // with e.g. 9.07 seconds on a ~3.33 second sequence.
        return FMath::Fmod(FMath::Max(0.f, Time), PlayLength);
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
            NormalizeAnimationAssetTime(AnimationAsset, AccumulatedTime, TopPlayer.IsLooping()),
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

    bool StabilizeAirLoopBlendStackBeforeUpdate(
        const FAnimationUpdateContext& Context,
        FAnimNode_MotionMatching& MotionMatchingNode,
        FCachedMotionMatchingNodeInfo& NodeInfo)
    {
        NodeInfo.bPreUpdateNormalizedLoopTime = false;
        NodeInfo.bPreUpdateCollapsedLoopStack = false;

        if (MotionMatchingNode.AnimPlayers.IsEmpty())
        {
            return false;
        }

        // Only one phase of the sustained Fall loop is meaningful.  Retaining
        // an old lower-weight phase lets it later become the evaluated player
        // during a relevance/weight change, which is why we collapse to the dominant player.
        // Looping time advancement is handled natively by UE's FBlendStackAnimPlayer;
        // forcibly calling Initialize/Restore with a normalized time rewinds the accumulated
        // clock and triggers AnimSequence.cpp's ensure(CurrentPosition >= PreviousPosition).
        NodeInfo.bPreUpdateCollapsedLoopStack = CollapseBlendStackToDominantPlayer(MotionMatchingNode);
        return NodeInfo.bPreUpdateCollapsedLoopStack;
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

    // Blend Stack stores the pose currently winning the blend at an arbitrary
    // array index while a blend is in progress.  For an air loop we must keep
    // that pose, not blindly index zero: keeping an older low-weight player
    // makes the loop jump between several sampled times when a duplicate
    // BlendTo request arrives.
    bool CollapseBlendStackToDominantPlayer(FAnimNode_MotionMatching& MotionMatchingNode)
    {
        if (MotionMatchingNode.AnimPlayers.Num() <= 1)
        {
            return false;
        }

        int32 DominantIndex = 0;
        float DominantWeight = MotionMatchingNode.AnimPlayers[0].GetBlendInWeight();
        for (int32 PlayerIndex = 1; PlayerIndex < MotionMatchingNode.AnimPlayers.Num(); ++PlayerIndex)
        {
            const float PlayerWeight = MotionMatchingNode.AnimPlayers[PlayerIndex].GetBlendInWeight();
            if (PlayerWeight > DominantWeight)
            {
                DominantWeight = PlayerWeight;
                DominantIndex = PlayerIndex;
            }
        }

        if (DominantIndex != 0)
        {
            MotionMatchingNode.AnimPlayers.Swap(0, DominantIndex);
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

    auto ProcessStructProp = [this](FStructProperty* StructProp)
    {
        if (!StructProp || !StructProp->Struct) return;

        if (StructProp->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))
        {
            for (const FCachedMotionMatchingNodeInfo& Existing : CachedMMNodes)
            {
                if (Existing.NodeProperty == StructProp) return;
            }

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
            for (const FCachedHistoryCollectorNodeInfo& Existing : CachedHistoryNodes)
            {
                if (Existing.NodeProperty == StructProp) return;
            }

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
    };

    UClass* AnimClass = InAnimInstance->GetClass();
    for (TFieldIterator<FProperty> PropIt(AnimClass); PropIt; ++PropIt)
    {
        ProcessStructProp(CastField<FStructProperty>(*PropIt));
    }

    if (UAnimBlueprintGeneratedClass* AnimBpgClass = Cast<UAnimBlueprintGeneratedClass>(AnimClass))
    {
        for (FStructProperty* NodeProp : AnimBpgClass->GetAnimNodeProperties())
        {
            ProcessStructProp(NodeProp);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[CACHE_NODES] Found %d MotionMatching nodes, %d HistoryCollector nodes in %s"),
        CachedMMNodes.Num(), CachedHistoryNodes.Num(), *GetNameSafe(InAnimInstance));

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
    StrafeMotionMatchingDebugAccumulator += InContext.GetDeltaTime();
    const int32 StrafeMotionMatchingDebugLevel = CVarStrafeMotionMatchingDebug.GetValueOnAnyThread();
    const bool bStrafeMotionMatchingSampleDue =
        StrafeMotionMatchingDebugLevel >= 2 && StrafeMotionMatchingDebugAccumulator >= 0.25f;
    if (bStrafeMotionMatchingSampleDue)
    {
        StrafeMotionMatchingDebugAccumulator = 0.f;
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
                const EStateControllerPresentationState PresentationState =
                    ThreadSafeData.StateController.PresentationState;
                const bool bIsTransitionState = IsTransitionMotionMatchingState(CurrentMotionState);
                const bool bIsJumpStartPhase =
                    PresentationState == EStateControllerPresentationState::TransitionToJump;
                // StateController owns direct clips.  Legacy CurrentState may
                // already be Idle/Locomotion while a visible Blend Stack TIP,
                // Start, Stop, Pivot or Land still owns output.
                const bool bIsProtectedOneShotState =
                    ThreadSafeData.StateController.bShouldOverrideMotionMatching ||
                    bIsTransitionState ||
                    bIsJumpStartPhase;
                // A direct State Controller one-shot owns its visual output.
                // Conversely, this flag is only raised for a moving Strafe
                // directional redirect, where PSD_Run_Tnasition contains the
                // Box/Diamond clips that a steady loop PSD cannot select.
                const bool bMovingStrafeTransitionQuery =
                    ThreadSafeData.InputData.bHasMoveInput &&
                    !ThreadSafeData.AirData.bIsInAir &&
                    !bIsProtectedOneShotState &&
                    ThreadSafeData.StateController.bUseLocomotionTransitionDatabase;
                const bool bForceMovingStrafeReselect =
                    bMovingStrafeTransitionQuery && !Info.bWasStrafeTurnReselectRequested;
                Info.bPreUpdateStrafeTurnReselect = bForceMovingStrafeReselect;
                const bool bIsAirLoopPhase =
                    ThreadSafeData.AirData.bIsInAir &&
                    !ThreadSafeData.AirData.bIsJumping &&
                    !ThreadSafeData.AirData.bIsFallOffStart &&
                    !ThreadSafeData.LandingData.bIsLanding &&
                    !ThreadSafeData.LandingData.bLandingRequested;

                // Normalize before FAnimNode_MotionMatching copies the top
                // Blend Stack time into SearchResult and extracts root motion.
                // Doing it after the graph update is one frame too late for a
                // stale looping Fall player.
                if (bIsAirLoopPhase)
                {
                    StabilizeAirLoopBlendStackBeforeUpdate(InContext, *MMNode, Info);
                }
                const bool bIsRemoteIdleHold =
                    bIsRemoteSimProxy &&
                    CurrentMotionState == ELocomotionState::Idle &&
                    !ThreadSafeData.InputData.bHasMoveInput &&
                    !bSearchResultDatabaseChanged &&
                    !bAppliedDatabaseChanged;
                const bool bForceLandRedirectReselection =
                    ThreadSafeData.StateController.bForceMotionMatchingReselection;
                if (bForceLandRedirectReselection)
                {
                    // Land -> MM is a graph hand-off, not a normal database
                    // change.  Force a fresh query from Pose History so the
                    // visible Land pose is the query source, rather than the
                    // MM node's old continuing pose behind the bool blend.
                    if (CurrentActivePoseSearchDatabase)
                    {
                        MMNode->SetDatabaseToSearch(
                            CurrentActivePoseSearchDatabase,
                            EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
                    }
                    else
                    {
                        MMNode->ResetDatabasesToSearch(
                            EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
                    }
                    Info.AppliedDatabase = CurrentActivePoseSearchDatabase;
                }
                else if (bAppliedDatabaseChanged)
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
                else if (bForceMovingStrafeReselect)
                {
                    // Do not invalidate the continuing pose here.  We merely
                    // make the next query compare it against Box/Diamond
                    // candidates in the transition PSD, which preserves a
                    // smooth loop when no directional clip is actually better.
                    MMNode->SetInterruptMode(EPoseSearchInterruptMode::ForceInterrupt);
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

                Info.bWasStrafeTurnReselectRequested = bMovingStrafeTransitionQuery;

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
                        if (bForceLandRedirectReselection)
                        {
                            // This frame must execute the reselect request;
                            // do not inherit the one-shot search suppression.
                            SearchThrottleTime = Info.DefaultSearchThrottleTime;
                        }
                        else if (bIsJumpStartPhase || (StateComp &&
                            (StateComp->bStartRequested || StateComp->bStopRequested || StateComp->bIsLanding)))
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
    const bool bStabilizeAirLoop =
        ThreadSafeData.AirData.bIsInAir &&
        !ThreadSafeData.AirData.bIsJumping &&
        !ThreadSafeData.AirData.bIsFallOffStart &&
        !ThreadSafeData.LandingData.bIsLanding &&
        !ThreadSafeData.LandingData.bLandingRequested;
    const bool bStabilizeFallOffLoop =
        ThreadSafeData.AirData.bIsInAir &&
        !ThreadSafeData.AirData.bIsJumping &&
        ThreadSafeData.AirData.bIsFallOffStart;
    if (bStabilizeJumpStart || bStabilizeRemoteTransition || bStabilizeAirLoop || bStabilizeFallOffLoop)
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
                    if (bStabilizeJumpStart)
                    {
                        Info.bPostUpdateRestoredTransitionStack =
                            StabilizeJumpStartBlendStackPlayer(InContext, *MMNode, Info);
                    }
                    else if (bStabilizeRemoteTransition)
                    {
                        Info.bPostUpdateRestoredTransitionStack =
                            StabilizeRemoteTransitionBlendStackPlayer(InContext, *MMNode, Info, UpdatedMotionState);
                    }
                    else
                    {
                        // The air PSD has a single loop asset.  The node must
                        // never mix multiple phase offsets of that same loop.
                        // This runs after the graph update, which is where
                        // duplicate BlendTo requests are materialized.
                        Info.bPostUpdateCollapsedTransitionStack =
                            CollapseBlendStackToDominantPlayer(*MMNode);
                    }
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
                if (Result.SelectedAnim.Get() != Info.LastStrafeDebugSelectedAnim.Get())
                {
                    const FString StateStr = StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(
                        static_cast<int64>(ThreadSafeData.StateController.PresentationState));
                    UE_LOG(LogTemp, Warning,
                        TEXT("[MM_CHOICE] MM selected asset -> %s (Prev: %s, Cost=%.2f, Time=%.2f) | State=%s | DB=%s"),
                        *GetNameSafe(Result.SelectedAnim.Get()),
                        *GetNameSafe(Info.LastStrafeDebugSelectedAnim.Get()),
                        Result.SearchCost,
                        Result.SelectedTime,
                        *StateStr,
                        *GetNameSafe(Result.SelectedDatabase.Get()));
                    Info.LastStrafeDebugSelectedAnim = Result.SelectedAnim.Get();
                }
            }
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

                    // This is intentionally distinct from p.MMDebugging: it
                    // answers the moving-Strafe question without flooding logs
                    // with landing/fall capture.  In particular it records
                    // whether a new Pose Search was permitted, whether the
                    // continuing pose won, and what trajectory the query saw.
                    const bool bMovingStrafePhase =
                        ThreadSafeData.InputData.bHasMoveInput &&
                        !ThreadSafeData.AirData.bIsInAir &&
                        !ThreadSafeData.StateController.bShouldOverrideMotionMatching;
                    const bool bStrafeSelectionChanged =
                        Info.LastStrafeDebugSelectedAnim.Get() != Result.SelectedAnim.Get() ||
                        FMath::Abs(Info.LastStrafeDebugSelectedTime - Result.SelectedTime) > 0.35f;

                    if (Result.SelectedAnim.Get() != Info.LastStrafeDebugSelectedAnim.Get())
                    {
                        const FString StateStr = StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(
                            static_cast<int64>(ThreadSafeData.StateController.PresentationState));
                        UE_LOG(LogTemp, Warning,
                            TEXT("[MM_CHOICE] MM selected asset changed -> %s (Prev: %s, Cost=%.2f, Time=%.2f) | State=%s | DB=%s"),
                            *GetNameSafe(Result.SelectedAnim.Get()),
                            *GetNameSafe(Info.LastStrafeDebugSelectedAnim.Get()),
                            Result.SearchCost,
                            Result.SelectedTime,
                            *StateStr,
                            *GetNameSafe(Result.SelectedDatabase.Get()));
                    }

                    if (StrafeMotionMatchingDebugLevel > 0 && bMovingStrafePhase &&
                        (bStrafeSelectionChanged || bStrafeMotionMatchingSampleDue))
                    {
                        const FTransform OwnerComponentTransform = AnimInstanceObj->GetOwningComponent()
                            ? AnimInstanceObj->GetOwningComponent()->GetComponentTransform()
                            : FTransform::Identity;
                        const FString InterruptModeName = ReadPoseSearchInterruptMode(Info, *MMNode);
                        const FString StrafeDebugLine = FString::Printf(
                            TEXT("[STRAFE_MM] Pawn=%s Node=%d Event=%s PSD=%s SelPSD=%s Asset=%s@%.3f Cost=%.3f Continue=%d Search=%d Throttle=%.3f Interrupt=%s TurnPSD=%d TurnReselect=%d Input=(R=%.2f,F=%.2f) VelLocal=(R=%.1f,F=%.1f) Accel=(%.1f,%.1f) AimYaw=%.1f Traj={%s %s} Stack={%s}"),
                            *DebugPawnName,
                            MotionMatchingNodeIndex,
                            bStrafeSelectionChanged ? TEXT("SELECTION") : TEXT("SAMPLE"),
                            *GetNameSafe(CurrentActivePoseSearchDatabase),
                            *GetNameSafe(Result.SelectedDatabase),
                            *GetNameSafe(Result.SelectedAnim),
                            Result.SelectedTime,
                            Result.SearchCost,
                            Result.bIsContinuingPoseSearch ? 1 : 0,
                            Info.bPreUpdateShouldSearch ? 1 : 0,
                            Info.PreUpdateThrottle,
                            *InterruptModeName,
                            ThreadSafeData.StateController.bUseLocomotionTransitionDatabase ? 1 : 0,
                            Info.bPreUpdateStrafeTurnReselect ? 1 : 0,
                            ThreadSafeData.InputData.MoveInput.X,
                            ThreadSafeData.InputData.MoveInput.Y,
                            ThreadSafeData.MovementData.VelocityLocal.Y,
                            ThreadSafeData.MovementData.VelocityLocal.X,
                            ThreadSafeData.MovementData.Acceleration.X,
                            ThreadSafeData.MovementData.Acceleration.Y,
                            ThreadSafeData.AimData.AimYaw,
                            *FormatTrajectorySample(ThreadSafeData.MovementData.Trajectory, OwnerComponentTransform, 0.5f),
                            *FormatTrajectorySample(ThreadSafeData.MovementData.Trajectory, OwnerComponentTransform, 1.0f),
                            *FormatCompactBlendStackHead(*MMNode));
                        UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *StrafeDebugLine);
                        Info.LastStrafeDebugSelectedAnim = Result.SelectedAnim.Get();
                        Info.LastStrafeDebugSelectedTime = Result.SelectedTime;
                    }

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

    // Ground contact and slope adaptation settings
    FootPlacementPlantSettingsDefault.DistanceToGround = 0.0f;
    FootPlacementPlantSettingsDefault.MaxExtensionRatio = 0.95f;
    FootPlacementPlantSettingsDefault.MinExtensionRatio = 0.1f;
    FootPlacementPlantSettingsDefault.AnkleTwistReduction = 0.75f;

    FootPlacementPlantSettingsStops.DistanceToGround = 0.0f;
    FootPlacementPlantSettingsStops.MaxExtensionRatio = 0.95f;
    FootPlacementPlantSettingsStops.MinExtensionRatio = 0.1f;
    FootPlacementPlantSettingsStops.AnkleTwistReduction = 0.75f;
    FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
    FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
    FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
    FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
    FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

    FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
    FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
    FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
    FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;

    TurnInPlaceFootPlacementAlpha = 1.0f;
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

        // The State Controller only needs the sustained loop and this one
        // moving-Strafe redirect PSD.  Keep the asset assignment self-healing
        // for existing ABP instances that predate LocomotionTransitionDatabase.
        if (!LocomotionTransitionDatabase)
        {
            LocomotionTransitionDatabase = LoadObject<UPoseSearchDatabase>(
                nullptr,
                TEXT("/Game/Anim_Logic/PSD/PSD_Run_Tnasition.PSD_Run_Tnasition"));
        }

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

        // --- Transition & Stop-to-Idle Diagnostics ---
        if (CachedBasePlayer && CachedBasePlayer->IsLocallyControlled())
        {
            const FMotionMatchingAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
            const FAnimThreadSafeData& ThreadSafeData = Proxy.ThreadSafeData;

            const FString StateName = UEnum::GetValueAsString(ThreadSafeData.StateController.PresentationState);
            const bool bOverrideMM = ThreadSafeData.StateController.bShouldOverrideMotionMatching;
            const FString AnimName = ThreadSafeData.StateController.SelectedAnimation ? ThreadSafeData.StateController.SelectedAnimation->GetName() : TEXT("None (MM Active)");
            const float Speed = ThreadSafeData.MovementData.Velocity.Size2D();
            const float Accel = ThreadSafeData.MovementData.Acceleration.Size2D();
            const FString DBName = CurrentActivePoseSearchDatabase ? CurrentActivePoseSearchDatabase->GetName() : TEXT("None");

            // 상태가 바뀔 때마다 상세 로그 출력
            static EStateControllerPresentationState LastLoggedState = EStateControllerPresentationState::None;
            static bool LastLoggedOverride = false;
            if (LastLoggedState != ThreadSafeData.StateController.PresentationState || LastLoggedOverride != bOverrideMM)
            {
                UE_LOG(LogTemp, Warning, TEXT("[AnimTransition] State: %s -> %s | OverrideMM: %s -> %s | Anim: %s | Speed: %.1f | DB: %s"),
                    *UEnum::GetValueAsString(LastLoggedState), *StateName,
                    LastLoggedOverride ? TEXT("TRUE") : TEXT("FALSE"), bOverrideMM ? TEXT("TRUE") : TEXT("FALSE"),
                    *AnimName, Speed, *DBName);

                LastLoggedState = ThreadSafeData.StateController.PresentationState;
                LastLoggedOverride = bOverrideMM;
            }

            // 화면 좌측 상단 실시간 HUD (초록색/노란색)
            if (GEngine)
            {
                FString HUDStr = FString::Printf(TEXT("[Anim HUD] State: %s | OverrideMM: %s | Anim: %s | Speed: %.1f | Accel: %.1f | DB: %s"),
                    *StateName,
                    bOverrideMM ? TEXT("TRUE (BlendStack)") : TEXT("FALSE (MotionMatching)"),
                    *AnimName, Speed, Accel, *DBName);

                FColor HUDColor = bOverrideMM ? FColor::Yellow : FColor::Green;
                GEngine->AddOnScreenDebugMessage(99991, 0.0f, HUDColor, HUDStr);
            }
        }
    }

    // 1. C++ 직접 상태 분기 및 알맞은 PSD 할당 (Chooser Table 미사용)
    // 모션 매칭 최적화 틱 스킵과 무관하게 데이터베이스 포인터는 항상 매 프레임 즉시 최신 상태로 유지합니다.
    CurrentActivePoseSearchDatabase = nullptr;

    if (CachedLocomotionStateComponent)
    {
        const bool bSprinting = CachedLocomotionStateComponent->bIsSprinting;
        switch (StateControllerPlaybackHoldState)
        {
        case EStateControllerPresentationState::TransitionToJump:
            CurrentActivePoseSearchDatabase = InAirDatabase;
            break;

        case EStateControllerPresentationState::TransitionToLand:
            CurrentActivePoseSearchDatabase = CachedLocomotionStateComponent->bLandWasMoving
                ? (CachedLocomotionStateComponent->bLandWasSprinting ? SprintLocomotionDatabase : LocomotionDatabase)
                : IdleDatabase;
            break;

        case EStateControllerPresentationState::LocomotionLoop:
            if (CachedLocomotionStateComponent->bIsInAir)
            {
                CurrentActivePoseSearchDatabase = InAirDatabase;
            }
            else
            {
                const bool bUseRunTransitionDatabase =
                    !bSprinting &&
                    CachedLocomotionStateComponent->bIsLocomotionTransitioning &&
                    LocomotionTransitionDatabase;
                CurrentActivePoseSearchDatabase = bSprinting
                    ? SprintLocomotionDatabase
                    : (bUseRunTransitionDatabase ? LocomotionTransitionDatabase : LocomotionDatabase);
            }
            break;

        case EStateControllerPresentationState::TransitionToStop:
        case EStateControllerPresentationState::IdleLoop:
        case EStateControllerPresentationState::TurnInPlace:
            CurrentActivePoseSearchDatabase = IdleDatabase;
            break;

        default:
            // Start and Pivot are direct assets.
            CurrentActivePoseSearchDatabase = bSprinting ? SprintLocomotionDatabase : LocomotionDatabase;
            break;
        }
    }

    // 최적화 틱 레이트에 맞추어 이번 프레임의 모션 매칭 평가 여부 결정
    if (!ShouldEvaluateMotionMatchingThisFrame(DeltaSeconds))
    {
        return;
    }

    if (false && CachedLocomotionStateComponent) // Legacy DB routing kept for reference only.
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

    // Direct Chooser one-shots own completion through their elapsed playback
    // time and component fallback timers.  Do not let animation Notifies end a
    // state: the same source clips are also used by Motion Matching and their
    // authored Notifies must remain cosmetic/gameplay-only.
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
    ThreadSafeData.StateController.bForceMotionMatchingReselection =
        CachedLocomotionStateComponent->ConsumeMotionMatchingReselectionRequest() ||
        bStateControllerForceMotionMatchingReselect;
    bStateControllerForceMotionMatchingReselect = false;
    ThreadSafeData.StateController.bUseLocomotionTransitionDatabase =
        CurrentActivePoseSearchDatabase &&
        LocomotionTransitionDatabase &&
        CurrentActivePoseSearchDatabase == LocomotionTransitionDatabase;

    // Movement Data
    const FAnimThreadSafeData& PreviousThreadSafeData =
        GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    ThreadSafeData.MovementData.Velocity = CachedLocomotionStateComponent->Velocity;
    ThreadSafeData.MovementData.VelocityLocal = CachedBasePlayer->GetActorTransform().InverseTransformVectorNoScale(CachedLocomotionStateComponent->Velocity);
    ThreadSafeData.MovementData.LastNonZeroVelocity = PreviousThreadSafeData.MovementData.LastNonZeroVelocity;
    if (!ThreadSafeData.MovementData.Velocity.IsNearlyZero(10.0f))
    {
        ThreadSafeData.MovementData.LastNonZeroVelocity = ThreadSafeData.MovementData.Velocity;
    }
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
    // Use transient input-derived requests, not persistent legacy states.
    // Otherwise a completed Stop can be resurrected after a stationary TIP.
    ThreadSafeData.GroundData.bStartRequested = CachedLocomotionStateComponent->bStartRequested;
    ThreadSafeData.GroundData.bStopRequested = CachedLocomotionStateComponent->bStopRequested;
    ThreadSafeData.GroundData.GroundMotionMode = static_cast<uint8>(CachedLocomotionStateComponent->CurrentState);

    // Air Data
    ThreadSafeData.AirData.bIsInAir = CachedLocomotionStateComponent->bIsInAir;
    ThreadSafeData.AirData.bIsJumping = CachedLocomotionStateComponent->bIsJumping;
    ThreadSafeData.AirData.bIsFallOffStart = CachedLocomotionStateComponent->bIsFallOffStart;
    ThreadSafeData.AirData.bJumpStartWasMoving = CachedLocomotionStateComponent->bJumpStartWasMoving;
    ThreadSafeData.AirData.JumpStartGroundSpeed = CachedLocomotionStateComponent->JumpStartGroundSpeed;
    ThreadSafeData.AirData.JumpStartMoveDirection = CachedLocomotionStateComponent->JumpStartMoveDirection;

    // Landing Data
    ThreadSafeData.LandingData.bIsLanding = CachedLocomotionStateComponent->bIsLanding;
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

        if (const ABowItem* EquippedBow = Cast<ABowItem>(CachedBasePlayer->EquippedItem))
        {
            if (const UBowComponent* BowComponent = EquippedBow->GetBowComponent())
            {
                ThreadSafeData.BowData.bIsAiming = BowComponent->IsAiming();
                ThreadSafeData.BowData.DrawAlpha = BowComponent->GetDrawAlpha();
                ThreadSafeData.BowData.bIsDrawing = ThreadSafeData.BowData.bIsAiming && ThreadSafeData.BowData.DrawAlpha > KINDA_SMALL_NUMBER;
                ThreadSafeData.BowData.bIsFullyDrawn = ThreadSafeData.BowData.bIsFullyDrawn || (ThreadSafeData.BowData.DrawAlpha >= 1.f - KINDA_SMALL_NUMBER);

                FTransform StringIKTargetWorldTransform = FTransform::Identity;
                const bool bShouldAttachStringToHand =
                    !ThreadSafeData.BowData.bIsReleasing &&
                    ThreadSafeData.BowData.DrawAlpha > KINDA_SMALL_NUMBER;

                if (bShouldAttachStringToHand &&
                    EquippedBow->GetStringIKTargetTransform(ThreadSafeData.BowData.DrawAlpha, StringIKTargetWorldTransform))
                {
                    if (const USkeletalMeshComponent* CharacterMesh = GetSkelMeshComponent())
                    {
                        ThreadSafeData.BowData.bHasStringIKTarget = true;
                        ThreadSafeData.BowData.StringIKTargetTransform =
                            StringIKTargetWorldTransform.GetRelativeTransform(CharacterMesh->GetComponentTransform());
                    }
                }
                else
                {
                    ThreadSafeData.BowData.bHasStringIKTarget = false;
                }
            }
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

    // This game-thread heartbeat deliberately does not depend on a Motion
    // Matching node having produced a selection. It therefore distinguishes a
    // routing/input failure from a legitimate "loop won the Pose Search" result.
    if (CVarStrafeMotionMatchingDebug.GetValueOnGameThread() > 0 && CachedBasePlayer &&
        CachedBasePlayer->IsLocallyControlled())
    {
        const double Now = GetWorld()->GetTimeSeconds();
        if (Now >= NextStrafeMotionMatchingGameThreadDebugTime)
        {
            NextStrafeMotionMatchingGameThreadDebugTime = Now + 0.25;
            const FString PresentationName = StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(
                static_cast<int64>(ThreadSafeData.StateController.PresentationState));
            const FString LegacyStateName = StaticEnum<ELocomotionState>()->GetNameStringByValue(
                static_cast<int64>(CachedLocomotionStateComponent->CurrentState));
            const FString StrafeLine = FString::Printf(
                TEXT("[STRAFE_MM_GT] Pawn=%s State=%s Present=%s Input=(R=%.2f,F=%.2f,H=%d) Ground=%.1f VelLocal=(R=%.1f,F=%.1f) Accel=(%.1f,%.1f) AimYaw=%.1f TurnAngle=%.1f Transition=%d TurnPSD=%d Override=%d PSD=%s Direct=%s"),
                *CachedBasePlayer->GetName(),
                *LegacyStateName,
                *PresentationName,
                ThreadSafeData.InputData.MoveInput.X,
                ThreadSafeData.InputData.MoveInput.Y,
                ThreadSafeData.InputData.bHasMoveInput ? 1 : 0,
                ThreadSafeData.LandingData.GroundSpeed,
                ThreadSafeData.MovementData.VelocityLocal.Y,
                ThreadSafeData.MovementData.VelocityLocal.X,
                ThreadSafeData.MovementData.Acceleration.X,
                ThreadSafeData.MovementData.Acceleration.Y,
                ThreadSafeData.AimData.AimYaw,
                ThreadSafeData.StateController.TrajectoryTurnAngleDegrees,
                CachedLocomotionStateComponent->bIsLocomotionTransitioning ? 1 : 0,
                ThreadSafeData.StateController.bUseLocomotionTransitionDatabase ? 1 : 0,
                ThreadSafeData.StateController.bShouldOverrideMotionMatching ? 1 : 0,
                *GetNameSafe(CurrentActivePoseSearchDatabase),
                *GetNameSafe(ThreadSafeData.StateController.SelectedAnimation));
            UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *StrafeLine);
        }
    }

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
    return OverlayTag.MatchesTag(Item_Weapon_Bow) || EquippedWeaponTag.MatchesTag(Item_Weapon_Bow) ||
           OverlayTag.MatchesTag(Item_Id_Weapon_Bow) || EquippedWeaponTag.MatchesTag(Item_Id_Weapon_Bow) ||
           OverlayTag.ToString().Contains(TEXT("Bow")) || EquippedWeaponTag.ToString().Contains(TEXT("Bow"));
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
    const bool bIsBow = WeaponData.OverlayTag.MatchesTag(Item_Weapon_Bow) ||
                        WeaponData.OverlayTag.MatchesTag(Item_Id_Weapon_Bow) ||
                        WeaponData.EquippedWeaponTag.MatchesTag(Item_Weapon_Bow) ||
                        WeaponData.EquippedWeaponTag.MatchesTag(Item_Id_Weapon_Bow) ||
                        WeaponData.OverlayTag.ToString().Contains(TEXT("Bow")) ||
                        WeaponData.EquippedWeaponTag.ToString().Contains(TEXT("Bow"));
    if (!bIsBow)
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

void UMotionMatchingAnimInstance::SetGaspOffsetRootTransform(const FTransform& InOffsetRootTransform)
{
    // Compatibility stub for ABP_Player until its old callback is removed.
    // TIP no longer consumes Offset Root Bone transforms.
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

float UMotionMatchingAnimInstance::GetThreadSafeFootPlacementAlpha() const
{
    const FAnimThreadSafeData& Data = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    if (Data.AirData.bIsInAir)
    {
        return 0.0f;
    }
    return Data.StateController.PresentationState == EStateControllerPresentationState::TurnInPlace
        ? TurnInPlaceFootPlacementAlpha
        : 1.0f;
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
    // Artistic is permanently Strafe.  Presentation is intentionally derived
    // from current input/kinematics and transient requests, not from the
    // legacy ELocomotionState.  The latter is retained for movement/component
    // compatibility, but it must never be allowed to replay Start/Stop after
    // a newer presentation phase (especially stationary TIP) has finished.
    const bool bShouldTurnInPlace = CachedLocomotionStateComponent->bShouldTurnInPlace;
    const bool bStartRequested = CachedLocomotionStateComponent->bStartRequested;
    const bool bStopRequested = CachedLocomotionStateComponent->bStopRequested;
    const bool bIsPivoting = CachedLocomotionStateComponent->bSharpTurnRequested;
    const bool bIsMoving = bHasMoveInput || GroundSpeed > 10.0f;
    // The Start request can predate the actual chooser selection (for example,
    // Shift may be pressed just after move input).  Therefore use the gait
    // frozen when the direct Start clip was selected, rather than either the
    // raw input edge or the mutable current StateControllerGait.
    // Releasing Sprint during an authored Sprint Start must bypass its remaining
    // one-shot hold and resume the regular locomotion MM database immediately.
    // Similarly, pressing Sprint during an authored Run Start must also bypass its remaining
    // one-shot hold and hand off immediately to Sprint Locomotion MM database!
    const bool bInterruptStartForGaitChange =
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart &&
        bHasMoveInput &&
        (bStateControllerSelectedSprintStart != CachedLocomotionStateComponent->bIsSprinting);
    const bool bInPlaybackHold = (StateControllerPlaybackHoldElapsed < StateControllerPlaybackHoldDuration);
    const float DesiredFacingDeltaYaw = CachedLocomotionStateComponent->DesiredFacingDeltaYaw;
    const float AbsDesiredFacingDeltaYaw = FMath::Abs(DesiredFacingDeltaYaw);
    const bool bActiveTurnInPlaceClip =
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace &&
        StateControllerSelectedAnimation != nullptr;
    // Do not manufacture a 090 clip for a small mouse adjustment. Once a
    // direct TIP was legitimately selected, however, keep it until its real
    // authored duration rather than cutting to Idle at the 0.75s re-check.
    const bool bKeepActiveTurnInPlaceClip = bActiveTurnInPlaceClip &&
        StateControllerPlaybackHoldElapsed < StateControllerPlaybackHoldDuration;
    const bool bCanStartTurnInPlace = bShouldTurnInPlace &&
        AbsDesiredFacingDeltaYaw >= StateControllerTurnInPlaceEntryAngle;
    const EStateControllerPresentationState HoldStateBeforeEvaluation = StateControllerPlaybackHoldState;
    const int32 SelectionRevisionBeforeEvaluation = StateControllerSelectionRevision;
    const bool bPlayerHasActionTag = CachedBasePlayer && CachedBasePlayer->GetAbilitySystemComponent() &&
        (CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Attacking) ||
         CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Damaged) ||
         CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Dead));
    const bool bPlayerActionFlag = CachedBasePlayer &&
        (CachedBasePlayer->bIsAttacking || CachedBasePlayer->bIsDodging ||
         CachedBasePlayer->bIsHitReacting || CachedBasePlayer->bIsPlayingCombatIntro);
    const bool bFullBodyMontagePlaying = Montage_IsPlaying(nullptr);
    const bool bActionMontageActive = bPlayerHasActionTag || bPlayerActionFlag || bFullBodyMontagePlaying;

    if (bActionMontageActive)
    {
        DesiredState = bHasMoveInput
            ? EStateControllerPresentationState::LocomotionLoop
            : EStateControllerPresentationState::IdleLoop;
    }
    // StartLanding deliberately keeps bIsInAir true until the landing pose is
    // released.  Landing must therefore take precedence over the air flag, unless TIP is strongly requested.
    else if (bLanding && !bCanStartTurnInPlace)
    {
        // Project_J's Strafe path enters its Land chooser on the impact frame.
        // Artistic already records an immutable impact direction, so diagonals
        // must use that snapshot directly rather than briefly falling through
        // to Motion Matching before their direct Land is selected.
        DesiredState = EStateControllerPresentationState::TransitionToLand;
    }
    else if (bInAir)
    {
        // JumpStart/FallOffStart are direct Blend Stack one-shots only.  Once
        // their component flags clear, the character is still airborne but the
        // output must return to the InAir PSD's Motion Matching node.  Routing
        // every airborne frame to TransitionToJump kept the direct branch
        // active forever, which is why the authored In Air Loop never became
        // visible.
        const bool bDirectAirOneShot =
            CachedLocomotionStateComponent->bIsJumping ||
            CachedLocomotionStateComponent->bIsFallOffStart;
        DesiredState = bDirectAirOneShot
            ? EStateControllerPresentationState::TransitionToJump
            : EStateControllerPresentationState::LocomotionLoop;
    }
    else if (bHasMoveInput)
    {
        // A fresh input during an authored Stop is a new movement episode, not
        // a request to reveal the Motion Matching fallback.  Project_J enters
        // Start again first; only a subsequent clear change of direction or
        // camera-facing intent may replace that Start with the locomotion PSD.
        if (bIsPivoting && (StateControllerPlaybackHoldState == EStateControllerPresentationState::LocomotionLoop || StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart))
        {
            DesiredState = EStateControllerPresentationState::TransitionToPivot;
        }
        else if (bInterruptStartForGaitChange)
        {
            DesiredState = EStateControllerPresentationState::LocomotionLoop;
            if (CachedLocomotionStateComponent)
            {
                CachedLocomotionStateComponent->InterruptStartForGaitChange();
            }
        }
        else if (bStartRequested ||
            StateControllerPlaybackHoldState == EStateControllerPresentationState::IdleLoop ||
            StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop ||
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
        // When there is no movement input (!bHasMoveInput):
        // 1. If TurnInPlace is active or requested, TurnInPlace takes priority over Stop/Idle
        if (bKeepActiveTurnInPlaceClip || bCanStartTurnInPlace)
        {
            DesiredState = EStateControllerPresentationState::TurnInPlace;
        }
        // 2. If we were already in TurnInPlace and finished turning, transition directly to IdleLoop
        else if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace)
        {
            DesiredState = EStateControllerPresentationState::IdleLoop;
        }
        // 3. Otherwise, check if a Stop transition is owed from previous movement/deceleration
        else
        {
            const bool bLocomotionStateStop = CachedLocomotionStateComponent &&
                CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop;
            const bool bStopHoldActive = bInPlaybackHold &&
                StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop;
            const bool bStopFallbackFromDeceleration = GroundSpeed > 10.0f;
            if (bStopRequested || bLocomotionStateStop || bStopHoldActive || bStopFallbackFromDeceleration ||
                (bInPlaybackHold && StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart))
            {
                DesiredState = EStateControllerPresentationState::TransitionToStop;
            }
            else
            {
                DesiredState = EStateControllerPresentationState::IdleLoop;
            }
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
    // Only preserve the authored Land while gameplay still owns a Landing
    // request.  A redirect has already changed the component to Locomotion;
    // holding the old clip in that case visually defeats the MM hand-off.
    const bool bGameplayLandStillActive =
        CachedLocomotionStateComponent->bIsLanding &&
        CachedLocomotionStateComponent->bLandingRequested;
    const bool bLandWantsSprint = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting;
    const bool bLandWasSprinting = (StateControllerLandGaitLock == EGaitIntent::Sprint);
    const bool bLandGaitChanged = (bLandWasSprinting != bLandWantsSprint);
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand &&
        bGameplayLandStillActive &&
        bReturningFromLandToGroundPresentation &&
        !bCanStartTurnInPlace &&
        !bLandGaitChanged &&
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
    bStateControllerInitialStartInputReselect = false;
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
            // Keyboard diagonals are assembled over one or two input updates:
            // e.g. Left Start can be selected before Forward reaches the
            // component, then ForwardLeft arrives on the next frame.  Treat
            // only that tiny, input-only window as a Start reselect. Camera
            // yaw changes always remain an intentional MM redirect.
            const bool bAssemblingInitialDiagonal =
                bStateControllerStartInputChanged &&
                !bStateControllerStartControlYawChanged &&
                StateControllerPlaybackHoldElapsed < StateControllerStartInputAssemblyWindow;
            if (bAssemblingInitialDiagonal)
            {
                bStateControllerInitialStartInputReselect = true;
            }
            else
            {
                const bool bContinueMoving = bIsMoving;
                DesiredState = bContinueMoving
                    ? EStateControllerPresentationState::LocomotionLoop
                    : EStateControllerPresentationState::TransitionToStop;
                StateControllerRequestedPresentationState = DesiredState;
            }
        }
    }

    if (DesiredState == EStateControllerPresentationState::TransitionToLand)
    {
        if (!bHasStateControllerLandGaitLock)
        {
            const bool bMovingLand = CachedLocomotionStateComponent->bLandWasMoving;
            if (bMovingLand)
            {
                // The selected Land must retain the gait at impact.  Reading the
                // live sprint input here can switch a Sprint Land to Run midway
                // through the first frame after Shift is released.
                StateControllerLandGaitLock = CachedLocomotionStateComponent->bLandWasSprinting ? EGaitIntent::Sprint : EGaitIntent::Run;
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

    StateControllerTurnInPlaceIndexForChooser = 0.0f;
    // The component still owns the semantic 090/180 bucket. The presentation
    // layer adds only the visible-clip entry gate, so a 30-degree adjustment
    // remains idle while an active authored turn is allowed to finish.
    if (bCanStartTurnInPlace)
    {
        const bool bLeft = DesiredFacingDeltaYaw < 0.0f;
        StateControllerTurnInPlaceIndexForChooser = bLeft
            ? (AbsDesiredFacingDeltaYaw >= 135.0f ? 2.0f : 1.0f)
            : (AbsDesiredFacingDeltaYaw >= 135.0f ? 4.0f : 3.0f);
    }

    StateControllerTurnInPlaceSelectionFacingDeltaYaw = DesiredFacingDeltaYaw;
    bStateControllerForceTurnInPlaceReselect = false;

    EvaluateStateControllerPlaybackHold(DesiredState);

    if (CVarStartDebug.GetValueOnGameThread() > 0 && bHasMoveInput)
    {
        const bool bEnteredStart =
            DesiredState == EStateControllerPresentationState::TransitionToStart &&
            (HoldStateBeforeEvaluation != EStateControllerPresentationState::TransitionToStart ||
             StateControllerSelectionRevision != SelectionRevisionBeforeEvaluation);
        const bool bBypassedExpectedStart =
            (HoldStateBeforeEvaluation == EStateControllerPresentationState::IdleLoop ||
             HoldStateBeforeEvaluation == EStateControllerPresentationState::TransitionToStop ||
             HoldStateBeforeEvaluation == EStateControllerPresentationState::TurnInPlace ||
             HoldStateBeforeEvaluation == EStateControllerPresentationState::None) &&
            DesiredState != EStateControllerPresentationState::TransitionToStart;
        const bool bStartInterrupted =
            HoldStateBeforeEvaluation == EStateControllerPresentationState::TransitionToStart &&
            DesiredState == EStateControllerPresentationState::LocomotionLoop &&
            (bStateControllerStartInputChanged || bStateControllerStartControlYawChanged);

        if (bEnteredStart || bBypassedExpectedStart || bStartInterrupted)
        {
            const FVector2D Input = CachedLocomotionStateComponent->CachedMoveInput;
            const float InputYaw = Input.IsNearlyZero()
                ? 0.0f
                : FMath::RadiansToDegrees(FMath::Atan2(Input.X, Input.Y));
            const EMovementDirection InputDirection = ResolveStateControllerDirectionFromInput(Input);
            UE_LOG(LogTemp, Display,
                TEXT("[SC_START] Event=%s Input=(R=%.2f,F=%.2f) Yaw=%.1f Dir=%s StartReq=%d HoldBefore=%d Desired=%d HoldAfter=%d Rev=%d Asset=%s Chooser=%s InputRedirect=%d(%.1f) YawRedirect=%d(%.1f)"),
                bStateControllerInitialStartInputReselect ? TEXT("ReselectedInitialDiagonal") :
                    (bEnteredStart ? TEXT("Selected") : (bStartInterrupted ? TEXT("InterruptedToMM") : TEXT("Bypassed"))),
                Input.X,
                Input.Y,
                InputYaw,
                *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(InputDirection)),
                bStartRequested ? 1 : 0,
                static_cast<int32>(HoldStateBeforeEvaluation),
                static_cast<int32>(DesiredState),
                static_cast<int32>(StateControllerPlaybackHoldState),
                StateControllerSelectionRevision,
                *GetNameSafe(StateControllerSelectedAnimation),
                *StateControllerLastChooserPath,
                bStateControllerStartInputChanged ? 1 : 0,
                StateControllerStartInputDeltaDegrees,
                bStateControllerStartControlYawChanged ? 1 : 0,
                StateControllerStartControlYawDeltaDegrees);
        }
    }

    if (CVarStopDebug.GetValueOnGameThread() > 0 && bStopRequested)
    {
        UE_LOG(LogTemp, Display,
            TEXT("[SC_STOP_CONTROLLER] Request=%d Desired=%d Hold=%d Input=%d Speed=%.1f TIP=%d Rev=%d Asset=%s Clock=%.3f/%.3f"),
            bStopRequested ? 1 : 0,
            static_cast<int32>(DesiredState),
            static_cast<int32>(StateControllerPlaybackHoldState),
            bHasMoveInput ? 1 : 0,
            GroundSpeed,
            bShouldTurnInPlace ? 1 : 0,
            StateControllerSelectionRevision,
            *GetNameSafe(StateControllerSelectedAnimation),
            StateControllerPlaybackHoldElapsed,
            StateControllerPlaybackHoldDuration);
    }

    // Level 2 is deliberately a polling probe. It must print even if a Stop
    // request is never generated, so we can distinguish bad input sampling
    // from a State Controller/Chooser failure without relying on another
    // event-driven debug channel.
    if (CVarStopDebug.GetValueOnGameThread() >= 2)
    {
        const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        if (Now >= NextStopDebugSampleTime)
        {
            UE_LOG(LogTemp, Display,
                TEXT("[SC_STOP_SAMPLE] Input=%d PrevInput=%d Ground=%.1f Legacy=%d StartReq=%d StopReq=%d TIP=%d Air=%d Land=%d Desired=%d Hold=%d Rev=%d Asset=%s Clock=%.3f/%.3f"),
                bHasMoveInput ? 1 : 0,
                CachedLocomotionStateComponent->bPrevHasMoveInput ? 1 : 0,
                GroundSpeed,
                static_cast<int32>(CachedLocomotionStateComponent->CurrentState),
                bStartRequested ? 1 : 0,
                bStopRequested ? 1 : 0,
                bShouldTurnInPlace ? 1 : 0,
                bInAir ? 1 : 0,
                bLanding ? 1 : 0,
                static_cast<int32>(DesiredState),
                static_cast<int32>(StateControllerPlaybackHoldState),
                StateControllerSelectionRevision,
                *GetNameSafe(StateControllerSelectedAnimation),
                StateControllerPlaybackHoldElapsed,
                StateControllerPlaybackHoldDuration);
            NextStopDebugSampleTime = Now + 0.25;
        }
    }

    // Stop is a persistent request until this point.  Consume only after the
    // State Controller has installed its direct one-shot hold, never merely
    // because legacy CurrentState happened to change.
    if (DesiredState == EStateControllerPresentationState::TransitionToStop &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop)
    {
        CachedLocomotionStateComponent->ConsumeStopPresentationRequest();
    }
}

void UMotionMatchingAnimInstance::EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState)
{
    FAnimThreadSafeData& ThreadSafeData = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    const bool bStateChanged = (DesiredState != StateControllerPlaybackHoldState);
    const EStateControllerPresentationState PreviousState = StateControllerPlaybackHoldState;
    const UAnimationAsset* PreviousSelectedAnimation = StateControllerSelectedAnimation;
    // This is intentionally a one-update signal.  It is consumed by an
    // AnimGraph OnUpdate function calling BlendStack::ForceBlendNextUpdate,
    // not by a per-frame Chooser query.
    bStateControllerForceBlendStackOnNextUpdate = false;

    // 원샷(Start/Stop/Land 등)에서 Loop로 전이될 때 직전 워핑 각도를 블렌드아웃 동안 보존
    const bool bPreviousWasOneShot =
        PreviousState == EStateControllerPresentationState::TransitionToStart ||
        PreviousState == EStateControllerPresentationState::TransitionToStop ||
        PreviousState == EStateControllerPresentationState::TransitionToJump ||
        PreviousState == EStateControllerPresentationState::TransitionToLand ||
        PreviousState == EStateControllerPresentationState::TransitionToPivot;
    if (bStateChanged && bPreviousWasOneShot)
    {
        if (bHasStateControllerOneShotOrientationWarpingAngle)
        {
            // 애님 그래프의 Blend Poses by bool 블렌드 시간(0.2s) 동안 각도/알파 유지
            StateControllerPostOneShotWarpingRemainingTime = 0.25f;
            StateControllerPostOneShotWarpingAngle = StateControllerOneShotOrientationWarpingAngle;
        }

        if (DesiredState == EStateControllerPresentationState::LocomotionLoop)
        {
            // Start/Stop 등 원샷에서 모션매칭으로 핸드오프될 때,
            // 블렌드 뒤에 숨어있던 낡은 Continuing Pose(정면 루프)를 즉시 파기하고
            // 현재 대각선 이동 궤적에 맞춰 첫 프레임에 강제 재검색!
            bStateControllerForceMotionMatchingReselect = true;
        }
    }

    // Project_J policy: a change to another semantic turn bucket preempts the
    // active clip immediately (especially important for reverse turns).  The
    // same bucket may replay after a short delay, allowing a long camera turn
    // to continue without waiting for the authored clip to finish.
    const int32 RequestedTurnIndex = FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser);
    const int32 ActiveTurnIndex = StateControllerActiveTurnInPlaceIndex;
    const bool bTurnInPlaceBucketChanged =
        RequestedTurnIndex > 0 &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace &&
        RequestedTurnIndex != ActiveTurnIndex;
    const bool bTurnInPlaceSameDirection =
        RequestedTurnIndex > 0 && RequestedTurnIndex == ActiveTurnIndex;
    const bool bTurnInPlaceReplayHasEnoughResidual =
        CachedLocomotionStateComponent &&
        FMath::Abs(CachedLocomotionStateComponent->DesiredFacingDeltaYaw) >=
            StateControllerTurnInPlaceReplayRemainingAngle;
    // Retain Project_J's two re-evaluation reasons. A semantic bucket change
    // (most importantly a reverse turn) always wins immediately. The 0.75s
    // same-direction path remains a timer, but it is allowed to restart an
    // authored 090 root track only when enough yaw remains to use that track.
    const bool bTurnInPlaceReplayDue =
        RequestedTurnIndex > 0 &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace &&
        DesiredState == EStateControllerPresentationState::TurnInPlace &&
        CachedLocomotionStateComponent && CachedLocomotionStateComponent->bShouldTurnInPlace &&
        (bTurnInPlaceBucketChanged ||
            (bTurnInPlaceSameDirection &&
                bTurnInPlaceReplayHasEnoughResidual &&
                StateControllerPlaybackHoldElapsed >= StateControllerTurnInPlaceReplayElapsed));
    bStateControllerForceTurnInPlaceReselect = bTurnInPlaceReplayDue;
    const bool bStartInputReselectDue =
        bStateControllerInitialStartInputReselect &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart &&
        DesiredState == EStateControllerPresentationState::TransitionToStart;

    bool bInterruptLandForMotionMatching = false;
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand &&
        DesiredState == EStateControllerPresentationState::LocomotionLoop)
    {
        const bool bWantsSprint = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsSprinting;
        const bool bLandWasSprinting = (StateControllerLandGaitLock == EGaitIntent::Sprint);

        if (!CachedLocomotionStateComponent || !CachedLocomotionStateComponent->bIsLanding ||
            (bLandWasSprinting != bWantsSprint))
        {
            bInterruptLandForMotionMatching = true;
        }
    }

    const bool bPlayerHasActionTag = CachedBasePlayer && CachedBasePlayer->GetAbilitySystemComponent() &&
        (CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Attacking) ||
         CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Damaged) ||
         CachedBasePlayer->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Dead));
    const bool bPlayerActionFlag = CachedBasePlayer &&
        (CachedBasePlayer->bIsAttacking || CachedBasePlayer->bIsDodging ||
         CachedBasePlayer->bIsHitReacting || CachedBasePlayer->bIsPlayingCombatIntro);
    const bool bFullBodyMontagePlaying = Montage_IsPlaying(nullptr);
    const bool bActionMontageActive = bPlayerHasActionTag || bPlayerActionFlag || bFullBodyMontagePlaying;

    const bool bActionMontageClearDue = bActionMontageActive &&
        (StateControllerSelectedAnimation != nullptr ||
         StateControllerPlaybackHoldState != DesiredState);

    const FString ReselectReason = (bStateChanged || bInterruptLandForMotionMatching || bTurnInPlaceReplayDue || bStartInputReselectDue || bActionMontageClearDue)
        ? FString::Printf(TEXT("StateChange=%d(%s->%s), LandInterrupt=%d, TIPReplay=%d, StartReselect=%d, MontageClear=%d"),
            bStateChanged ? 1 : 0,
            *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(PreviousState)),
            *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(DesiredState)),
            bInterruptLandForMotionMatching ? 1 : 0,
            bTurnInPlaceReplayDue ? 1 : 0,
            bStartInputReselectDue ? 1 : 0,
            bActionMontageClearDue ? 1 : 0)
        : FString();

    if (bStateChanged || bInterruptLandForMotionMatching || bTurnInPlaceReplayDue || bStartInputReselectDue || bActionMontageClearDue)
    {
        StateControllerPlaybackHoldState = DesiredState;
        StateControllerPlaybackHoldElapsed = 0.0f;

        if (DesiredState == EStateControllerPresentationState::TransitionToLand)
        {
            bHasStateControllerLandingDirectionLatch = false;
            StateControllerLandingSteeringTargetYaw = 0.0f;
            StateControllerLandingOrientationWarpingAngle = 0.0f;
        }

        StateControllerPresentationState = StateControllerPlaybackHoldState;
        StateControllerMovementDirection = CurrentMovementDirection;
        StateControllerPreviousMovementDirection = MovementDirectionLastFrame;
        if (DesiredState == EStateControllerPresentationState::TransitionToStop)
        {
            if (PreviousState == EStateControllerPresentationState::TransitionToLand &&
                bHasStateControllerLandingDirectionLatch)
            {
                // Project_J preserves the last valid strafe sector when stopped.
                // Velocity is already near zero here, so recomputing it would first
                // choose Forward/Backward and visibly flash the wrong Stop clip.
                CurrentMovementDirection = StateControllerLandingDirectionLatch;
                StateControllerMovementDirection = StateControllerLandingDirectionLatch;
                StateControllerPreviousMovementDirection = StateControllerLandingDirectionLatch;
            }
            else
            {
                // 키보드 비동기 릴리즈(손가락 시차) 보정:
                // 직전(StateControllerStopDiagonalReleaseWindow 이내)에 대각선 이동 중이었다면,
                // 손가락이 미세하게 먼저 떼진 키로 인해 단일 축(Right/Left/Forward)으로 튀는 것을 방지하고
                // Chooser Table 평가 전에 대각선 방향을 확정(Latch)합니다.
                const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                if ((CurrentTime - LastDiagonalMovementDirectionTime) <= StateControllerStopDiagonalReleaseWindow)
                {
                    CurrentMovementDirection = LastDiagonalMovementDirection;
                    StateControllerMovementDirection = LastDiagonalMovementDirection;
                    StateControllerPreviousMovementDirection = LastDiagonalMovementDirection;
                }
            }
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
        bStateControllerSelectedSprintStart =
            DesiredState == EStateControllerPresentationState::TransitionToStart &&
            StateControllerGait == EGaitIntent::Sprint;

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

            // Project_J freezes the direction used by a direct Strafe clip at
            // selection time.  Re-reading velocity each update lets braking or
            // a second WASD key bend an authored Start/Stop/Pivot in mid-play.
            bHasStateControllerOneShotOrientationWarpingAngle = false;
            StateControllerOneShotOrientationWarpingAngle = 0.0f;
            if (DesiredState != EStateControllerPresentationState::TurnInPlace &&
                DesiredState != EStateControllerPresentationState::TransitionToStart)
            {
                FVector2D DirectionInput = CachedLocomotionStateComponent
                    ? CachedLocomotionStateComponent->CachedMoveInput
                    : FVector2D::ZeroVector;
                if (DesiredState == EStateControllerPresentationState::TransitionToJump &&
                    CachedLocomotionStateComponent &&
                    !CachedLocomotionStateComponent->JumpStartMoveDirection.IsNearlyZero())
                {
                    DirectionInput = CachedLocomotionStateComponent->JumpStartMoveDirection;
                }
                else if (DesiredState == EStateControllerPresentationState::TransitionToLand &&
                    CachedLocomotionStateComponent &&
                    !CachedLocomotionStateComponent->LandMoveDirection.IsNearlyZero())
                {
                    DirectionInput = CachedLocomotionStateComponent->LandMoveDirection;
                }
                else if (DesiredState == EStateControllerPresentationState::TransitionToStop)
                {
                    const bool bIsDiagonalStop =
                        (StateControllerMovementDirection == EMovementDirection::ForwardLeft ||
                         StateControllerMovementDirection == EMovementDirection::ForwardRight ||
                         StateControllerMovementDirection == EMovementDirection::BackwardLeft ||
                         StateControllerMovementDirection == EMovementDirection::BackwardRight);

                    if (bIsDiagonalStop)
                    {
                        // 대각선 정지의 경우, 감속 중 미세 속도 흔들림이나 손가락 시차로 각도가 틀어지는 것을 방지하기 위해
                        // 확정된 대각선 섹터의 이상적인 방향 벡터(FL=-45°, FR=+45°, BL=-135°, BR=+135°)를 사용하여 Warping을 정확히 고정합니다.
                        switch (StateControllerMovementDirection)
                        {
                        case EMovementDirection::ForwardLeft:  DirectionInput = FVector2D(-1.0f, 1.0f); break;
                        case EMovementDirection::ForwardRight: DirectionInput = FVector2D(1.0f, 1.0f); break;
                        case EMovementDirection::BackwardLeft: DirectionInput = FVector2D(-1.0f, -1.0f); break;
                        case EMovementDirection::BackwardRight: DirectionInput = FVector2D(1.0f, -1.0f); break;
                        default: break;
                        }
                    }
                    else
                    {
                        // 일반 단일 축 Stop은 직전 이동 속도(Velocity)를 로컬 좌표로 변환하여 방향을 캡처
                        if (CachedBasePlayer)
                        {
                            const FVector WorldVel = CachedBasePlayer->GetVelocity();
                            const FVector LocalVel = CachedBasePlayer->GetActorTransform().InverseTransformVector(WorldVel);
                            if (!LocalVel.IsNearlyZero(10.0f))
                            {
                                DirectionInput = FVector2D(LocalVel.Y, LocalVel.X); // X=Right, Y=Forward
                            }
                        }

                        // 속도가 이미 0에 가깝다면 Chooser가 선택한 MovementDirection으로 폴백
                        if (DirectionInput.IsNearlyZero())
                        {
                            switch (StateControllerMovementDirection)
                            {
                            case EMovementDirection::Left:         DirectionInput = FVector2D(-1.0f, 0.0f); break;
                            case EMovementDirection::Right:        DirectionInput = FVector2D(1.0f, 0.0f); break;
                            case EMovementDirection::Backward:     DirectionInput = FVector2D(0.0f, -1.0f); break;
                            case EMovementDirection::Forward:      DirectionInput = FVector2D(0.0f, 1.0f); break;
                            default: break;
                            }
                        }
                    }
                }

                if (!DirectionInput.IsNearlyZero())
                {
                    StateControllerOneShotOrientationWarpingAngle = FMath::RadiansToDegrees(
                        FMath::Atan2(DirectionInput.X, DirectionInput.Y));
                    bHasStateControllerOneShotOrientationWarpingAngle = true;
                }
            }
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
        // These values were refreshed before entering this function.  Do not
        // clear them here: the nested TIP Chooser reads them on this first
        // selection frame.
        bStateControllerIsHeavyLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bUseHeavyLand;
        bStateControllerIsMovingLand = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bLandWasMoving;
        bStateControllerIsInAir = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsInAir;
        bStateControllerIsJumping = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsJumping;
        bStateControllerIsFallOff = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bIsFallOffStart;
        bStateControllerShouldTurnInPlace = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bShouldTurnInPlace;

        // The Land Chooser calls BlueprintThreadSafe getters.  NativeUpdate's
        // normal proxy pack happens later, so without this pre-publish the
        // first Land selection reads the previous landing's Heavy/Light flag.
        // Publish the complete impact snapshot before evaluating child rows.
        if (CachedLocomotionStateComponent)
        {
            ThreadSafeData.LandingData.bUseHeavyLand = CachedLocomotionStateComponent->bUseHeavyLand;
            ThreadSafeData.LandingData.bLandWasMoving = CachedLocomotionStateComponent->bLandWasMoving;
            ThreadSafeData.LandingData.bLandWasSprinting = CachedLocomotionStateComponent->bLandWasSprinting;
            ThreadSafeData.LandingData.LandStartFallSpeed = CachedLocomotionStateComponent->LandStartFallSpeed;
            ThreadSafeData.LandingData.LandStartGroundSpeed = CachedLocomotionStateComponent->LandStartGroundSpeed;
            ThreadSafeData.LandingData.LandMoveDirection = CachedLocomotionStateComponent->LandMoveDirection;
            ThreadSafeData.LandingData.bIsLanding = CachedLocomotionStateComponent->bIsLanding;
            ThreadSafeData.LandingData.bLandingRequested = CachedLocomotionStateComponent->bLandingRequested;
        }

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
        else if (DesiredState == EStateControllerPresentationState::TransitionToJump && CachedLocomotionStateComponent &&
            CachedLocomotionStateComponent->bJumpStartWasMoving &&
            !CachedLocomotionStateComponent->JumpStartMoveDirection.IsNearlyZero())
        {
            // CMC may not have accelerated yet on the first jump frame. Use the
            // accepted ground launch snapshot rather than stale/current velocity
            // so the direct Jump chooser receives the intended diagonal sector.
            StateControllerPreviousMovementDirection = CurrentMovementDirection;
            CurrentMovementDirection = ResolveStateControllerDirectionFromInput(
                CachedLocomotionStateComponent->JumpStartMoveDirection);
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
            const float LandingDirectionLocalYaw = FMath::RadiansToDegrees(FMath::Atan2(
                CachedLocomotionStateComponent->LandMoveDirection.X,
                CachedLocomotionStateComponent->LandMoveDirection.Y));
            StateControllerLandingSteeringTargetYaw = CachedBasePlayer
                ? CachedBasePlayer->GetActorRotation().Yaw + LandingDirectionLocalYaw
                : LandingDirectionLocalYaw;
            StateControllerLandingOrientationWarpingAngle = LandingDirectionLocalYaw;
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
            const bool bLandChooserEvaluation = DesiredState == EStateControllerPresentationState::TransitionToLand;
            const bool bProxyHeavyBeforeChooser = ThreadSafeData.LandingData.bUseHeavyLand;
            const bool bGetterHeavyBeforeChooser = GetThreadSafeIsHeavyLand();
            const bool bComponentHeavyBeforeChooser = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bUseHeavyLand;
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
            if (CVarAnimStateControllerDebug.GetValueOnGameThread() > 0 && bLandChooserEvaluation)
            {
                UE_LOG(LogMotionMatchingCapture, Display,
                    TEXT("[SC_LAND_CHOOSER] Impact=%.1f Threshold=%.1f ComponentHeavy=%d ReflectedHeavy=%d ProxyHeavy=%d GetterHeavy=%d Moving=%d Direction=%s Result=%s Path=%s"),
                    CachedLocomotionStateComponent ? CachedLocomotionStateComponent->LandStartFallSpeed : 0.0f,
                    CachedLocomotionStateComponent ? CachedLocomotionStateComponent->HeavyLandSpeedThreshold : 0.0f,
                    bComponentHeavyBeforeChooser ? 1 : 0,
                    bStateControllerIsHeavyLand ? 1 : 0,
                    bProxyHeavyBeforeChooser ? 1 : 0,
                    bGetterHeavyBeforeChooser ? 1 : 0,
                    CachedLocomotionStateComponent && CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(StateControllerMovementDirection)),
                    *GetNameSafe(StateControllerSelectedAnimation),
                    *StateControllerLastChooserPath);
            }
            if (!StateControllerSelectedAnimation)
            {
                StateControllerLastChooserPath += TEXT(" -> <No Animation Row>");
            }
            // Keep the entire output structure, exactly as Project_J does.
            // StartTime/BlendTime remain one atomic authored contract with the
            // chosen asset rather than unrelated transient float values.
            StateControllerSelectedAnimationOutput = ChooserOutputs;
            const float DefaultBlendTime = DesiredState == EStateControllerPresentationState::TurnInPlace
                ? StateControllerTurnInPlaceDefaultBlendTime
                : 0.2f;
            StateControllerSelectedAnimationBlendTime = StateControllerSelectedAnimationOutput.BlendTime > 0.0f
                ? StateControllerSelectedAnimationOutput.BlendTime
                : DefaultBlendTime;
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
            if (DesiredState == EStateControllerPresentationState::TurnInPlace)
            {
                StateControllerActiveTurnInPlaceIndex = FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser);
            }
            else
            {
                StateControllerActiveTurnInPlaceIndex = 0;
            }
            ++StateControllerSelectionRevision;
            // When AnimGraph pins (Animation Asset, BlendTime, etc.) are connected,
            // the Blend Stack node automatically performs a BlendTo when the asset changes.
            // Firing ForceBlendOnNextUpdate at the same time causes UE5 to log
            // "multiple BlendTo requests during the same frame".
            // Force Blend is therefore only needed when the newly selected one-shot asset
            // is identical to the previously playing asset (e.g. continuous same-direction TIP
            // replay or reselection), which the pin alone cannot detect as a new transition.
            const bool bSameAssetReplay = (PreviousSelectedAnimation != nullptr &&
                PreviousSelectedAnimation == StateControllerSelectedAnimation);
            bStateControllerForceBlendStackOnNextUpdate =
                bEnteringOneShot && StateControllerSelectedAnimation != nullptr && bSameAssetReplay;

            if (DesiredState == EStateControllerPresentationState::TransitionToStop)
            {
                bDebugStopDiagnosticActive = true;
                DebugStopDiagnosticFrame = 0;
                DebugStopDiagnosticStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

                const FVector WorldVel = CachedBasePlayer ? CachedBasePlayer->GetVelocity() : FVector::ZeroVector;
                const FVector LocalVel = CachedBasePlayer ? CachedBasePlayer->GetActorTransform().InverseTransformVector(WorldVel) : FVector::ZeroVector;
                const FVector2D MoveInput = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->CachedMoveInput : FVector2D::ZeroVector;
                const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                const double DiagTimeDelta = CurrentTime - LastDiagonalMovementDirectionTime;

                float CurveStrafeWarpVal = 0.0f;
                float CurveWarpVal = 0.0f;
                GetCurveValue(FName(TEXT("Enable_StrafeWarping")), CurveStrafeWarpVal);
                GetCurveValue(FName(TEXT("Enable_Warping")), CurveWarpVal);

                const float ActorYaw = CachedBasePlayer ? CachedBasePlayer->GetActorRotation().Yaw : 0.0f;
                const float ControlYaw = CachedBasePlayer ? CachedBasePlayer->GetControlRotation().Yaw : 0.0f;
                const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
                const float MeshYaw = MeshComp ? MeshComp->GetComponentRotation().Yaw : 0.0f;
                const float RootBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("root"))).Yaw : 0.0f;
                const float PelvisBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("pelvis"))).Yaw : 0.0f;
                const float DesiredFacingDelta = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->DesiredFacingDeltaYaw : 0.0f;

                UE_LOG(LogTemp, Warning, TEXT("==================== [STOP_DIAG][ENTRY] ===================="));
                UE_LOG(LogTemp, Warning, TEXT("  [1. Input & Spd] GroundSpd=%.1f | WorldVel=(X=%.1f,Y=%.1f,Z=%.1f) Yaw=%.1f | LocalVel=(X=%.1f,Y=%.1f) | MoveInput=(X=%.2f,Y=%.2f) HasInput=%d"),
                    CachedLocomotionStateComponent ? CachedLocomotionStateComponent->GroundSpeed : 0.0f,
                    WorldVel.X, WorldVel.Y, WorldVel.Z, WorldVel.Rotation().Yaw,
                    LocalVel.X, LocalVel.Y,
                    MoveInput.X, MoveInput.Y,
                    CachedLocomotionStateComponent && CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0);
                UE_LOG(LogTemp, Warning, TEXT("  [2. Orientations] ActorYaw=%.1f | CamYaw=%.1f | MeshYaw=%.1f | RootBoneYaw=%.1f | PelvisYaw=%.1f | VelYaw=%.1f | DesiredFacingDelta=%.1f"),
                    ActorYaw, ControlYaw, MeshYaw, RootBoneYaw, PelvisBoneYaw, WorldVel.Rotation().Yaw, DesiredFacingDelta);
                UE_LOG(LogTemp, Warning, TEXT("  [3. Direction] RawDir=%s | LastDiag=%s (TimeDelta=%.3fs, Window=%.3fs, LatchHit=%d) -> FinalDir=%s"),
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(CurrentMovementDirection)),
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(LastDiagonalMovementDirection)),
                    DiagTimeDelta,
                    StateControllerStopDiagonalReleaseWindow,
                    DiagTimeDelta <= StateControllerStopDiagonalReleaseWindow ? 1 : 0,
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(StateControllerMovementDirection)));
                UE_LOG(LogTemp, Warning, TEXT("  [4. Foot Phase] LeftContact=%.3f RightContact=%.3f Delta=%.3f -> ChosenFoot=%s (HasCurves=%d)"),
                    CachedStateControllerLeftFootContact,
                    CachedStateControllerRightFootContact,
                    CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact,
                    StateControllerOneShotFoot == EStateControllerOneShotFoot::Left ? TEXT("Left") : TEXT("Right"),
                    bHasStateControllerFootContactCurves ? 1 : 0);
                UE_LOG(LogTemp, Warning, TEXT("  [5. Chooser Output] Asset=%s | StartTime=%.3fs | BlendTime=%.3fs | ClipLength=%.3fs | HoldDuration=%.3fs | Chooser=%s"),
                    *GetNameSafe(StateControllerSelectedAnimation),
                    StateControllerSelectedAnimationStartTime,
                    StateControllerSelectedAnimationBlendTime,
                    StateControllerSelectedAnimation ? StateControllerSelectedAnimation->GetPlayLength() : 0.0f,
                    StateControllerPlaybackHoldDuration,
                    *StateControllerLastChooserPath);
                UE_LOG(LogTemp, Warning, TEXT("  [6. Warping Setup] OneShotWarpAngle=%.2f deg (HasAngle=%d) | Curve(StrafeWarp=%.2f, Warping=%.2f)"),
                    StateControllerOneShotOrientationWarpingAngle,
                    bHasStateControllerOneShotOrientationWarpingAngle ? 1 : 0,
                    CurveStrafeWarpVal,
                    CurveWarpVal);
                UE_LOG(LogTemp, Warning, TEXT("============================================================"));
            }
            else if (DesiredState == EStateControllerPresentationState::TransitionToStart)
            {
                bDebugStartDiagnosticActive = true;
                DebugStartDiagnosticFrame = 0;

                const float ActorYaw = CachedBasePlayer ? CachedBasePlayer->GetActorRotation().Yaw : 0.0f;
                const float ControlYaw = CachedBasePlayer ? CachedBasePlayer->GetControlRotation().Yaw : 0.0f;
                const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
                const float MeshYaw = MeshComp ? MeshComp->GetComponentRotation().Yaw : 0.0f;
                const float RootBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("root"))).Yaw : 0.0f;
                const float PelvisBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("pelvis"))).Yaw : 0.0f;

                UE_LOG(LogTemp, Warning, TEXT("==================== [START_DIAG][ENTRY] ===================="));
                UE_LOG(LogTemp, Warning, TEXT("  Asset: %s | WarpAngle: %.2f | Dir: %s | Spd: %.1f"),
                    *GetNameSafe(StateControllerSelectedAnimation),
                    StateControllerOneShotOrientationWarpingAngle,
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(StateControllerMovementDirection)),
                    CachedLocomotionStateComponent ? CachedLocomotionStateComponent->GroundSpeed : 0.0f);
                UE_LOG(LogTemp, Warning, TEXT("  Orientations: ActorYaw=%.1f | CamYaw=%.1f | MeshYaw=%.1f | RootBoneYaw=%.1f | PelvisYaw=%.1f"),
                    ActorYaw, ControlYaw, MeshYaw, RootBoneYaw, PelvisBoneYaw);
                UE_LOG(LogTemp, Warning, TEXT("============================================================="));
            }

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
            StateControllerActiveTurnInPlaceIndex = 0;
            ++StateControllerSelectionRevision;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[RESELECT_EVENT] Frame=%d | %s -> %s | Selected: %s (Prev: %s) | Reason: [%s] | Rev=%d | ForceBlendStack=%d | Start=%.3f | Blend=%.3f"),
            DebugStopDiagnosticFrame,
            *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(PreviousState)),
            *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(DesiredState)),
            *GetNameSafe(StateControllerSelectedAnimation),
            *GetNameSafe(PreviousSelectedAnimation),
            *ReselectReason,
            StateControllerSelectionRevision,
            bStateControllerForceBlendStackOnNextUpdate ? 1 : 0,
            StateControllerSelectedAnimationStartTime,
            StateControllerSelectedAnimationBlendTime);
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
    ThreadSafeData.StateController.SelectedAnimationElapsedTime = StateControllerPlaybackHoldElapsed;
    ThreadSafeData.StateController.bSelectedAnimationShouldLoop = bStateControllerSelectedAnimationShouldLoop;
    ThreadSafeData.StateController.bHasSelectedAnimation = StateControllerSelectedAnimation != nullptr;
    ThreadSafeData.StateController.SelectionRevision = StateControllerSelectionRevision;
    ThreadSafeData.StateController.bForceBlendStackOnNextUpdate = bStateControllerForceBlendStackOnNextUpdate;
    ThreadSafeData.StateController.bShouldOverrideMotionMatching =
        ThreadSafeData.StateController.bHasSelectedAnimation &&
        StateControllerPlaybackHoldState != EStateControllerPresentationState::LocomotionLoop &&
        StateControllerPlaybackHoldState != EStateControllerPresentationState::IdleLoop;

    // Match Project_J's combat-Strafe contract: a Land and an immediate Stop
    // hand-off share the exact impact direction.  The current trajectory can
    // collapse to zero on input release, so it is not a valid Steering target
    // for these two one-shots.
    const bool bBlendStackClipActive = ThreadSafeData.StateController.bHasSelectedAnimation;
    const bool bMovingForSteering =
        ThreadSafeData.MovementData.Velocity.Size2D() > 10.0f ||
        ThreadSafeData.InputData.bHasMoveInput;
    const bool bAirborneForSteering = ThreadSafeData.AirData.bIsInAir;
    const bool bUseLatchedLandingSteering =
        bHasStateControllerLandingDirectionLatch &&
        (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand ||
         StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop);
    const bool bEnableBlendStackSteering = bBlendStackClipActive &&
        (bMovingForSteering || bAirborneForSteering || bUseLatchedLandingSteering);
    ThreadSafeData.StateController.BlendStackSteeringAlpha = bEnableBlendStackSteering ? 1.0f : 0.0f;
    // Project_J gates authored TIP Steering from the locomotion phase itself.
    // Presentation may still be finishing its current direct clip while the
    // next gameplay evaluation falls below the 30-degree selection threshold;
    // do not drop Steering during that visual tail.
    const bool bTurnInPlaceClipActive = bBlendStackClipActive &&
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace;
    // This alpha is intentionally independent from the generic movement
    // Steering alpha.  The AnimGraph multiplies it with the authored
    // Enable_TurnInPlaceSteering curve, so visual steering exists only during
    // the intended portion of a TIP clip.
    ThreadSafeData.StateController.TurnInPlaceSteeringAlpha = bTurnInPlaceClipActive ? 1.0f : 0.0f;

    if (bTurnInPlaceClipActive && CachedBasePlayer)
    {
        // TIP follows camera/control facing, never movement trajectory.  Actor
        // yaw is advanced separately from the selected sequence's root track.
        ThreadSafeData.StateController.BlendStackSteeringTargetOrientation = FRotator(
            0.0f,
            CachedBasePlayer->GetControlRotation().Yaw,
            0.0f);
    }
    else if (bUseLatchedLandingSteering && CachedBasePlayer)
    {
        ThreadSafeData.StateController.BlendStackSteeringTargetOrientation = FRotator(
            0.0f,
            StateControllerLandingSteeringTargetYaw,
            0.0f);
    }
    else if (const FTransformTrajectorySample* FutureFacingSample = FindClosestTrajectorySample(
        ThreadSafeData.MovementData.Trajectory, 0.5f))
    {
        const FRotator FutureFacing = FutureFacingSample->GetTransform().Rotator();
        ThreadSafeData.StateController.BlendStackSteeringTargetOrientation =
            FRotator(0.0f, FutureFacing.Yaw, 0.0f);
    }
    else if (!ThreadSafeData.MovementData.Velocity.IsNearlyZero(10.0f))
    {
        ThreadSafeData.StateController.BlendStackSteeringTargetOrientation =
            FRotator(0.0f, ThreadSafeData.MovementData.Velocity.Rotation().Yaw, 0.0f);
    }
    else
    {
        ThreadSafeData.StateController.BlendStackSteeringTargetOrientation = FRotator::ZeroRotator;
    }

    // 원샷(Start/Stop 등) 종료 후 Blend Stack이 모션매칭으로 블렌드아웃되는 동안 타이머 감쇄
    if (StateControllerPostOneShotWarpingRemainingTime > 0.0f)
    {
        StateControllerPostOneShotWarpingRemainingTime = FMath::Max(0.0f, StateControllerPostOneShotWarpingRemainingTime - DeltaTime);
    }

    // Project_J's Strafe contract keeps the direction that selected a direct
    // one-shot, rather than re-deriving it from a velocity that may already be
    // zero.  The authored `enable_warping` curve remains the final per-frame
    // gate in the Blend Stack graph.
    const bool bDirectStrafeOneShot = bBlendStackClipActive &&
        !ThreadSafeData.StateController.bSelectedAnimationShouldLoop &&
        (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop ||
         StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToJump ||
         StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand ||
         StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToPivot);

    const bool bInPostOneShotBlendOut = !bDirectStrafeOneShot && (StateControllerPostOneShotWarpingRemainingTime > 0.0f);

    float OrientationWarpingAngle = 0.0f;
    bool bHasOrientationWarpingDirection = false;
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart)
    {
        // Start는 Chooser가 8방향 에셋(Forward, Backward, Left, Right, 대각 4방향)을 직접 관리하므로
        // Orientation Warping을 사용하지 않고 순수 애니메이션으로 출발합니다.
        OrientationWarpingAngle = 0.0f;
        bHasOrientationWarpingDirection = false;
    }
    else if (bUseLatchedLandingSteering)
    {
        OrientationWarpingAngle = StateControllerLandingOrientationWarpingAngle;
        bHasOrientationWarpingDirection = true;
    }
    else if (bDirectStrafeOneShot && bHasStateControllerOneShotOrientationWarpingAngle)
    {
        OrientationWarpingAngle = StateControllerOneShotOrientationWarpingAngle;
        bHasOrientationWarpingDirection = true;
    }
    else if (bInPostOneShotBlendOut)
    {
        // 원샷에서 모션매칭/아이들로 넘어가는 0.2초 블렌딩 동안 직전 원샷 각도를 그대로 유지하여 정면(0도) 튐 방지
        OrientationWarpingAngle = StateControllerPostOneShotWarpingAngle;
        bHasOrientationWarpingDirection = true;
    }
    else if (!ThreadSafeData.MovementData.VelocityLocal.IsNearlyZero(10.0f))
    {
        OrientationWarpingAngle = FMath::RadiansToDegrees(FMath::Atan2(
            ThreadSafeData.MovementData.VelocityLocal.Y,
            ThreadSafeData.MovementData.VelocityLocal.X));
        bHasOrientationWarpingDirection = true;
    }
    else if (ThreadSafeData.InputData.bHasMoveInput)
    {
        // MoveInput is already character-local in this project: X=right, Y=forward.
        OrientationWarpingAngle = FMath::RadiansToDegrees(FMath::Atan2(
            ThreadSafeData.InputData.MoveInput.X,
            ThreadSafeData.InputData.MoveInput.Y));
        bHasOrientationWarpingDirection = true;
    }
    ThreadSafeData.StateController.CombatStateOrientationWarpingAngle = OrientationWarpingAngle;
    ThreadSafeData.StateController.CombatStateOrientationWarpingAlpha =
        (bDirectStrafeOneShot || bInPostOneShotBlendOut) && bHasOrientationWarpingDirection ? 1.0f : 0.0f;

    const FVector VelocityDirection = ThreadSafeData.MovementData.Velocity.GetSafeNormal2D();
    const FVector AccelerationDirection = ThreadSafeData.MovementData.Acceleration.GetSafeNormal2D();
    ThreadSafeData.StateController.TrajectoryTurnAngleDegrees =
        !VelocityDirection.IsNearlyZero() && !AccelerationDirection.IsNearlyZero()
        ? FRotator::NormalizeAxis(AccelerationDirection.Rotation().Yaw - VelocityDirection.Rotation().Yaw)
        : 0.0f;

    if (CachedLocomotionStateComponent)
    {
        ThreadSafeData.StateController.TurnInPlaceRootYawDelta = CachedLocomotionStateComponent->TurnInPlaceRootYawDelta;
    }

    StateControllerPresentationState = StateControllerPlaybackHoldState;
    const bool bHoldingStopDirection =
        StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop;
    if (!bHoldingStopDirection)
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
    bStateControllerShouldTurnInPlace = CachedLocomotionStateComponent && CachedLocomotionStateComponent->bShouldTurnInPlace;

    if (bDebugStopDiagnosticActive)
    {
        DebugStopDiagnosticFrame++;
        const bool bIsStillInStop = (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStop);

        if (bIsStillInStop)
        {
            // 정지 시작 후 첫 20프레임은 매 프레임 연속 출력, 그 이후는 5프레임 간격으로 출력 (최대 60프레임)
            if (DebugStopDiagnosticFrame <= 20 || (DebugStopDiagnosticFrame % 5 == 0 && DebugStopDiagnosticFrame <= 60))
            {
                const float ActorYaw = CachedBasePlayer ? CachedBasePlayer->GetActorRotation().Yaw : 0.0f;
                const float ControlYaw = CachedBasePlayer ? CachedBasePlayer->GetControlRotation().Yaw : 0.0f;
                const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
                const float MeshYaw = MeshComp ? MeshComp->GetComponentRotation().Yaw : 0.0f;
                const float RootBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("root"))).Yaw : 0.0f;
                const float PelvisBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("pelvis"))).Yaw : 0.0f;
                const FVector Vel = ThreadSafeData.MovementData.Velocity;
                const float VelYaw = Vel.IsNearlyZero(5.0f) ? 0.0f : Vel.Rotation().Yaw;
                const FVector LastVel = ThreadSafeData.MovementData.LastNonZeroVelocity;
                const float LastVelYaw = LastVel.IsNearlyZero(5.0f) ? 0.0f : LastVel.Rotation().Yaw;
                const FVector LocalVel = ThreadSafeData.MovementData.VelocityLocal;
                const float DesiredFacingDelta = CachedLocomotionStateComponent ? CachedLocomotionStateComponent->DesiredFacingDeltaYaw : 0.0f;
                const float SteeringTargetYaw = ThreadSafeData.StateController.BlendStackSteeringTargetOrientation.Yaw;

                float CurveStrafeWarpVal = 0.0f;
                float CurveWarpVal = 0.0f;
                GetCurveValue(FName(TEXT("Enable_StrafeWarping")), CurveStrafeWarpVal);
                GetCurveValue(FName(TEXT("Enable_Warping")), CurveWarpVal);

                UE_LOG(LogTemp, Warning,
                    TEXT("[STOP_DIAG][TICK #%02d] Elapsed=%.3f/%.3f | Spd=%.1f LocVel=(%.1f,%.1f)"),
                    DebugStopDiagnosticFrame,
                    StateControllerPlaybackHoldElapsed,
                    StateControllerPlaybackHoldDuration,
                    Vel.Size2D(),
                    LocalVel.X, LocalVel.Y);
                UE_LOG(LogTemp, Warning,
                    TEXT("  -> Orientations: ActorYaw=%.1f | CamYaw=%.1f | MeshYaw=%.1f | RootBoneYaw=%.1f | PelvisYaw=%.1f | VelYaw=%.1f | LastVelYaw=%.1f | DesFacingDelta=%.1f"),
                    ActorYaw, ControlYaw, MeshYaw, RootBoneYaw, PelvisBoneYaw, VelYaw, LastVelYaw, DesiredFacingDelta);
                UE_LOG(LogTemp, Warning,
                    TEXT("  -> Warping & State: WarpAngle=%.1f WarpAlpha=%.2f | SteeringYaw=%.1f | Anim=%s | Rev=%d | Dir=%s | Curves(StrafeWarp=%.2f, Warping=%.2f)"),
                    ThreadSafeData.StateController.CombatStateOrientationWarpingAngle,
                    ThreadSafeData.StateController.CombatStateOrientationWarpingAlpha,
                    SteeringTargetYaw,
                    *GetNameSafe(ThreadSafeData.StateController.SelectedAnimation),
                    ThreadSafeData.StateController.SelectionRevision,
                    *StaticEnum<EMovementDirection>()->GetNameStringByValue(static_cast<int64>(StateControllerMovementDirection)),
                    CurveStrafeWarpVal,
                    CurveWarpVal);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("==================== [STOP_DIAG][EXIT] ===================="));
            UE_LOG(LogTemp, Warning, TEXT("  Stop ended at Frame #%d | TotalElapsed=%.3fs / TargetHold=%.3fs | NextState=%s | RemainingSpeed=%.1f"),
                DebugStopDiagnosticFrame,
                StateControllerPlaybackHoldElapsed,
                StateControllerPlaybackHoldDuration,
                *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(StateControllerPlaybackHoldState)),
                ThreadSafeData.MovementData.Velocity.Size2D());
            UE_LOG(LogTemp, Warning, TEXT("============================================================"));

            bDebugStopDiagnosticActive = false;
        }
    }

    if (bDebugStartDiagnosticActive)
    {
        DebugStartDiagnosticFrame++;
        const bool bIsStillInStart = (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart);

        if (bIsStillInStart)
        {
            if (DebugStartDiagnosticFrame <= 15 || DebugStartDiagnosticFrame % 5 == 0)
            {
                const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
                const float RootBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("root"))).Yaw : 0.0f;
                const float PelvisBoneYaw = MeshComp ? MeshComp->GetSocketRotation(FName(TEXT("pelvis"))).Yaw : 0.0f;
                float CurveWarpVal = 0.0f;
                GetCurveValue(FName(TEXT("Enable_Warping")), CurveWarpVal);

                UE_LOG(LogTemp, Warning,
                    TEXT("[START_DIAG][TICK #%02d] Elapsed=%.3f/%.3f | RootYaw=%.1f | PelvisYaw=%.1f | WarpAngle=%.1f (Alpha=%.2f, Curve=%.2f)"),
                    DebugStartDiagnosticFrame,
                    StateControllerPlaybackHoldElapsed,
                    StateControllerPlaybackHoldDuration,
                    RootBoneYaw, PelvisBoneYaw,
                    ThreadSafeData.StateController.CombatStateOrientationWarpingAngle,
                    ThreadSafeData.StateController.CombatStateOrientationWarpingAlpha,
                    CurveWarpVal);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("==================== [START_DIAG][EXIT] ===================="));
            UE_LOG(LogTemp, Warning, TEXT("  Start finished -> NextState=%s | Elapsed=%.3fs | HoldRemainingWarpAngle=%.1f (Time=%.2fs)"),
                *StaticEnum<EStateControllerPresentationState>()->GetNameStringByValue(static_cast<int64>(StateControllerPlaybackHoldState)),
                StateControllerPlaybackHoldElapsed,
                StateControllerPostOneShotWarpingAngle,
                StateControllerPostOneShotWarpingRemainingTime);
            UE_LOG(LogTemp, Warning, TEXT("============================================================"));

            bDebugStartDiagnosticActive = false;
        }
    }

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

    const bool bStopDiagnostic =
        CachedLocomotionStateComponent->bStopRequested ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToStop ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToStop;
	const bool bTurnInPlace =
		StateController.PresentationState == EStateControllerPresentationState::TurnInPlace ||
		StateControllerRequestedPresentationState == EStateControllerPresentationState::TurnInPlace ||
		CachedLocomotionStateComponent->bTurnInPlacePhaseActive;
    const bool bLandDiagnostic =
        LocomotionState == ELocomotionState::Landing ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToLand ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToLand;
    const bool bAirDiagnostic =
        CachedLocomotionStateComponent->bIsInAir ||
        StateController.PresentationState == EStateControllerPresentationState::TransitionToJump ||
        StateControllerRequestedPresentationState == EStateControllerPresentationState::TransitionToJump;
    // The Land exit transition itself is the key evidence for this issue.  It
    // must remain visible for the single frame after the component has already
    // changed to Idle/Locomotion, when the ordinary "currently Landing" filter
    // would otherwise suppress the trace.
    const bool bLandExitEvent = bComponentEventChanged &&
        (CachedLocomotionStateComponent->GetStateControllerDebugLastEvent().Contains(TEXT("Landing")) ||
         CachedLocomotionStateComponent->GetStateControllerDebugLastEvent().Contains(TEXT("Land clip")));
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const bool bOneShotSampleDue = (bLandDiagnostic || bAirDiagnostic) && StateController.bShouldOverrideMotionMatching &&
        StateController.bHasSelectedAnimation && Now >= NextStateControllerOneShotDebugTime;

    if (!((bPresentationChanged || bSelectionChanged) && (bDirectOneShot || bStopDiagnostic)) &&
        !bOneShotSampleDue && !bComponentEventChanged)
    {
        return;
    }

    if (bTurnInPlace && (bPresentationChanged || bSelectionChanged))
    {
        const float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
        const float ControlYaw = CachedBasePlayer->GetControlRotation().Yaw;
        UE_LOG(LogMotionMatchingCapture, Display,
			TEXT("[SC_TIP] Pawn=%s Rev=%d Asset=%s Index=%.0f ActiveIndex=%d Elapsed=%.3f/%.3f Start=%.3f Blend=%.3f Loop=%d OverrideMM=%d SelectedYaw=%.1f CurrentYaw=%.1f ActorYaw=%.1f ControlYaw=%.1f RootYaw=%.2f Phase=%d PhaseTime=%.3f RawEntry=%d Retarget=%d ForceBlend=%d"),
            *CachedBasePlayer->GetName(),
			StateController.SelectionRevision,
			*GetNameSafe(StateController.SelectedAnimation),
			StateControllerTurnInPlaceIndexForChooser,
			StateControllerActiveTurnInPlaceIndex,
            StateControllerPlaybackHoldElapsed,
            StateControllerPlaybackHoldDuration,
            StateController.SelectedAnimationStartTime,
            StateController.SelectedAnimationBlendTime,
            StateController.bSelectedAnimationShouldLoop ? 1 : 0,
            StateController.bShouldOverrideMotionMatching ? 1 : 0,
            StateControllerTurnInPlaceSelectionFacingDeltaYaw,
            CachedLocomotionStateComponent->DesiredFacingDeltaYaw,
            ActorYaw,
            ControlYaw,
			CachedLocomotionStateComponent->TurnInPlaceRootYawDelta,
			CachedLocomotionStateComponent->bTurnInPlacePhaseActive ? 1 : 0,
			CachedLocomotionStateComponent->TurnInPlacePhaseElapsed,
			FMath::Abs(CachedLocomotionStateComponent->DesiredFacingDeltaYaw) >= 30.0f ? 1 : 0,
			bStateControllerForceTurnInPlaceReselect ? 1 : 0,
            StateController.bForceBlendStackOnNextUpdate ? 1 : 0);
    }

    if (bStopDiagnostic && (bPresentationChanged || bSelectionChanged || bComponentEventChanged))
    {
        UE_LOG(LogMotionMatchingCapture, Display,
            TEXT("[SC_STOP] Pawn=%s Requested=%d Presentation=%d LegacyState=%d Input=%d PrevInput=%d Speed=%.1f StopRequest=%d TIP=%d Rev=%d Asset=%s Elapsed=%.3f/%.3f"),
            *CachedBasePlayer->GetName(),
            static_cast<int32>(StateControllerRequestedPresentationState),
            static_cast<int32>(StateController.PresentationState),
            static_cast<int32>(LocomotionState),
            CachedLocomotionStateComponent->bHasMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->bPrevHasMoveInput ? 1 : 0,
            CachedLocomotionStateComponent->GroundSpeed,
            CachedLocomotionStateComponent->bStopRequested ? 1 : 0,
            CachedLocomotionStateComponent->bShouldTurnInPlace ? 1 : 0,
            StateController.SelectionRevision,
            *GetNameSafe(StateController.SelectedAnimation),
            StateControllerPlaybackHoldElapsed,
            StateControllerPlaybackHoldDuration);
    }

    // Keep the capture focused on Land/Air hand-offs.  Start/Stop/Pivot/TIP
    // samples remain suppressed so one jump gives a usable timeline.
    if (!bLandDiagnostic && !bLandExitEvent && !bAirDiagnostic)
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
        : (bPresentationChanged ? TEXT("Presentation") : TEXT("LandSample"));

    UE_LOG(LogMotionMatchingCapture, Display,
        TEXT("[SC_TRACE] Reason=%s Pawn=%s Locomotion=%s Requested=%s Presentation=%s Rev=%d OverrideMM=%d Asset=%s Start=%.3f Length=%.3f Elapsed=%.3f Blend=%.3f Loop=%d PSD=%s Input=(R=%.2f,F=%.2f) Speed=%.1f Direction=%s PrevDirection=%s LandStopLatch=%s Pivot=%d InputTurn=%.1f TrajectoryTurn=%.1f PivotThreshold=%.1f PivotMinSpeed=%.1f RedirectThreshold=%.1f Steering=%d TargetYaw=%.1f StartInputChanged=%d(%.1f/%.1f) StartYawChanged=%d(%.1f/%.1f) LandDir=(R=%.2f,F=%.2f) LandDirSource=%s LandDiagonal=%d LandDirectAtImpact=1 EffectiveLandMin=%.2f LandCompletionLead=%.2f LandRedirect=Input%d(%.1f/%.1f) Yaw%d(%.1f/%.1f) Timer=%.3f Gait=%s Foot=%s Jump=%d FallOff=%d Landing=%d HeavyLand=%d MovingLand=%d MovingLandSprint=%d LandingFromFallOff=%d LastFallSpeed=%.1f LandTime=%.3f LandPostInput=%d(%.3f/%.3f) Chooser=%s Outputs=%s"),
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
        StateController.TrajectoryTurnAngleDegrees,
        CachedLocomotionStateComponent->PivotAngleThreshold,
        CachedLocomotionStateComponent->PivotMinSpeed,
        CachedLocomotionStateComponent->SharpTurnAngleThreshold,
        StateController.BlendStackSteeringAlpha > 0.0f ? 1 : 0,
        StateController.BlendStackSteeringTargetOrientation.Yaw,
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
        CachedLocomotionStateComponent->bLastLandingInputDirectionChanged ? 1 : 0,
        CachedLocomotionStateComponent->LastLandingInputDirectionDelta,
        CachedLocomotionStateComponent->LandingInputDirectionInterruptAngle,
        CachedLocomotionStateComponent->bLastLandingControlYawChanged ? 1 : 0,
        CachedLocomotionStateComponent->LastLandingControlYawDelta,
        CachedLocomotionStateComponent->LandingControlYawInterruptAngle,
        CachedLocomotionStateComponent->GetStateControllerDebugLandingFallbackRemaining(),
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
        CachedLocomotionStateComponent->bLandingReceivedMoveInput ? 1 : 0,
        CachedLocomotionStateComponent->LandingPostTouchdownMoveInputTime,
        CachedLocomotionStateComponent->LandingExitStopInputHoldTime,
        *StateControllerLastChooserPath,
        *StateControllerLastChooserOutputTrace);

    if (GEngine && CVarAnimStateControllerDebug.GetValueOnGameThread() > 0)
    {
        const FString ScreenDebug = FString::Printf(
            TEXT("[AnimState] Presentation: %s | Requested: %s | State: %s | Asset: %s | LandMoving: %d | StopReq: %d | TIP: %d | Speed: %.1f"),
            *PresentationName,
            *RequestedPresentationName,
            *LocomotionName,
            *GetNameSafe(StateController.SelectedAnimation),
            CachedLocomotionStateComponent->bLandWasMoving ? 1 : 0,
            CachedLocomotionStateComponent->bStopRequested ? 1 : 0,
            CachedLocomotionStateComponent->bShouldTurnInPlace ? 1 : 0,
            CachedLocomotionStateComponent->GroundSpeed);
        GEngine->AddOnScreenDebugMessage(9999, 0.0f, FColor::Cyan, ScreenDebug);

        const FAnimWeaponUpperBodyData& WData = ThreadSafeData.WeaponUpperBodyData;
        const FString WeaponDebug = FString::Printf(
            TEXT("[WeaponOverlay] EquippedTag: %s | OverlayTag: %s | Index: %d | Alpha: %.1f | Mode: %s | Override: %d"),
            *WData.EquippedWeaponTag.ToString(),
            *WData.OverlayTag.ToString(),
            WData.OverlayIndex,
            WData.UpperBodyAlpha,
            *StaticEnum<EWeaponUpperBodyOverlayState>()->GetNameStringByValue(static_cast<int64>(WData.OverlayState)),
            WData.bShouldOverrideUpperBody ? 1 : 0);
        GEngine->AddOnScreenDebugMessage(9998, 0.0f, FColor::Emerald, WeaponDebug);
    }

    if (bComponentEventChanged)
    {
        UE_LOG(LogMotionMatchingCapture, Display,
            TEXT("[SC_COMPONENT] Pawn=%s EventRev=%d Event=%s Current=%s Air=%d PhysicalAir=%d Landing=%d Requested=%d LandMoving=%d LandPostInput=%d(%.3f/%.3f) HasInput=%d PrevInput=%d Input=(%.2f,%.2f) LandTime=%.3f MinLand=%.3f FallSpeed=%.1f"),
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
            CachedLocomotionStateComponent->LandingPostTouchdownMoveInputTime,
            CachedLocomotionStateComponent->LandingExitStopInputHoldTime,
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
    NextStateControllerPivotDebugTime = Now;

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

    const bool bIsDiagonal =
        (CurrentMovementDirection == EMovementDirection::ForwardLeft ||
         CurrentMovementDirection == EMovementDirection::ForwardRight ||
         CurrentMovementDirection == EMovementDirection::BackwardLeft ||
         CurrentMovementDirection == EMovementDirection::BackwardRight);
    if (bIsDiagonal && ThreadSafeData.InputData.bHasMoveInput)
    {
        LastDiagonalMovementDirection = CurrentMovementDirection;
        LastDiagonalMovementDirectionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
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

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerSelectedAnimationElapsedTime() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.SelectedAnimationElapsedTime;
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

bool UMotionMatchingAnimInstance::GetThreadSafeStateControllerForceBlendStackOnNextUpdate() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>()
        .ThreadSafeData.StateController.bForceBlendStackOnNextUpdate;
}

float UMotionMatchingAnimInstance::GetStateControllerTurnInPlaceIndexForChooser() const
{
    return StateControllerTurnInPlaceIndexForChooser;
}

bool UMotionMatchingAnimInstance::GetThreadSafeShouldOverrideMotionMatching() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.bShouldOverrideMotionMatching;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.TurnInPlaceSteeringAlpha;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerBlendStackSteeringAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.StateController.BlendStackSteeringAlpha;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerCombatStateOrientationWarpingAngle() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>()
        .ThreadSafeData.StateController.CombatStateOrientationWarpingAngle;
}

float UMotionMatchingAnimInstance::GetThreadSafeStateControllerCombatStateOrientationWarpingAlpha() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>()
        .ThreadSafeData.StateController.CombatStateOrientationWarpingAlpha;
}

FRotator UMotionMatchingAnimInstance::GetThreadSafeStateControllerDesiredFacingRotator() const
{
    // Compatibility alias for the pin used by the existing Blend Stack graph.
    // It deliberately no longer returns the TIP-only DesiredFacingDeltaYaw.
    return GetThreadSafeStateControllerBlendStackSteeringTargetOrientation();
}

FRotator UMotionMatchingAnimInstance::GetThreadSafeStateControllerBlendStackSteeringTargetOrientation() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>()
        .ThreadSafeData.StateController.BlendStackSteeringTargetOrientation;
}

FVector UMotionMatchingAnimInstance::GetThreadSafeLastNonZeroVelocity() const
{
    return GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData.MovementData.LastNonZeroVelocity;
}

EOffsetRootBoneMode UMotionMatchingAnimInstance::GetThreadSafeOffsetRootRotationMode() const
{
	const FAnimThreadSafeData& Data = GetProxyOnAnyThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
	const bool bIsTurnInPlace =
		Data.StateController.PresentationState == EStateControllerPresentationState::TurnInPlace;

	// Match Project_J's TIP visual-root contract.  During the selected direct
	// turn clip, Steering must be allowed to retain and smoothly interpolate its
	// rotational offset.  Returning Release here every frame cancels that offset
	// while the clip is still turning, which makes the authored root yaw look
	// short and then snap back toward Idle at the end of the clip.
	return Data.StateController.bHasSelectedAnimation &&
		bIsTurnInPlace &&
		!Data.AirData.bIsInAir
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
    // This getter is bound only by the game-thread Chooser evaluation.  The
    // reflected value is published immediately before EvaluateObjectChooserBase;
    // reading the animation proxy here was one update behind and selected the
    // previous landing's Heavy/Light row on the first frame.
    return bStateControllerIsHeavyLand;
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
    return false;
}
