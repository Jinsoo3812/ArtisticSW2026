#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Animation/SWTrajectoryComponent.h"
#include "CharacterTrajectoryComponent.h"
#include "ChooserFunctionLibrary.h"
#include "Chooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<int32> CVarMotionMatchingDebugLogging(
    TEXT("p.MMDebugging"),
    1,
    TEXT("Motion Matching landing diagnostics. 0: Disabled, 1: Enabled"),
    ECVF_Default
);

DEFINE_LOG_CATEGORY_STATIC(LogMotionMatchingCapture, Log, All);

namespace
{
    constexpr bool bSearchFallOffEveryUpdate = true;
    constexpr float FallOffSearchThrottleTime = 0.12f;
    constexpr float FallOffActiveSearchDuration = 0.45f;
    constexpr float SuppressedSearchThrottleTime = 3600.0f;

    void ClearMotionMatchingCaptureLog()
    {
        const FString LogFilePath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("MMCapture.log"));
        IFileManager::Get().Delete(*LogFilePath, false, true);
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

    FString FormatTrajectorySample(const FTransformTrajectory& Trajectory, const FTransform& ReferenceTransform, float TargetTime)
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

    void AppendMotionMatchingCaptureLine(const FString& Line)
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
    const bool bCaptureMotionMatchingFrame =
        bCaptureFrame &&
        (ThreadSafeData.AirData.bIsFallOffStart ||
            bIsAirLoopDebugPhase ||
            ThreadSafeData.LandingData.bIsLanding ||
            ThreadSafeData.LandingData.bLandingRequested);

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

                const UPoseSearchDatabase* SelectedDatabaseBeforeUpdate =
                    MMNode->GetMotionMatchingState().SearchResult.SelectedDatabase.Get();
                const bool bSearchResultDatabaseChanged = SelectedDatabaseBeforeUpdate != CurrentActivePoseSearchDatabase.Get();
                const bool bAppliedDatabaseChanged = Info.AppliedDatabase != CurrentActivePoseSearchDatabase;
                if (bAppliedDatabaseChanged)
                {
                    if (CurrentActivePoseSearchDatabase)
                    {
                        MMNode->SetDatabaseToSearch(
                            CurrentActivePoseSearchDatabase,
                            EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
                    }
                    else
                    {
                        MMNode->ResetDatabasesToSearch(
                            EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
                    }
                    Info.AppliedDatabase = CurrentActivePoseSearchDatabase;
                }

                if (Info.SearchThrottleTimeProperty)
                {
                    const bool bIsAirLoopPhase =
                        ThreadSafeData.AirData.bIsInAir &&
                        !ThreadSafeData.AirData.bIsJumping &&
                        !ThreadSafeData.AirData.bIsFallOffStart &&
                        !ThreadSafeData.LandingData.bIsLanding &&
                        !ThreadSafeData.LandingData.bLandingRequested;
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
                    else if (bIsAirLoopPhase && !bAppliedDatabaseChanged)
                    {
                        SearchThrottleTime = SuppressedSearchThrottleTime;
                    }
                    Info.SearchThrottleTimeProperty->SetPropertyValue_InContainer(MMNode, SearchThrottleTime);
                }

            }
        }
        ++MotionMatchingNodeIndex;
    }

    FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InGroupRelevancyName);

    if (bCaptureMotionMatchingFrame)
    {
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

                    const FString NodeDebugLine = FString::Printf(
                        TEXT("[MMCAP_NODE] Anim=%s Node=%d Property=%s RequestedPSD=%s SelectedDB=%s SelectedAsset=%s AssetDir=%s Time=%.3f Cost=%.3f Continue=%d Prev=%s Mirrored=%d Loop=%d PlayRate=%.3f Blend=(%.2f,%.2f,%.2f) Throttle=%.3f FallOff=%d SearchEveryUpdate=%d DBChanged=%d StackNum=%d"),
                        *AnimInstanceObj->GetName(),
                        MotionMatchingNodeIndex,
                        *Info.NodeProperty->GetName(),
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
                        bSearchFallOffEveryUpdate ? 1 : 0,
                        bDatabaseChanged ? 1 : 0,
                        MMNode->AnimPlayers.Num());
                    UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *NodeDebugLine);
                    AppendMotionMatchingCaptureLine(NodeDebugLine);

                    const FString BlendStackDebugLine = FString::Printf(
                        TEXT("[MMCAP_BLENDSTACK] Anim=%s Node=%d FallOff=%d Stack=%s"),
                        *AnimInstanceObj->GetName(),
                        MotionMatchingNodeIndex,
                        bIsFallOffStartPhase ? 1 : 0,
                        *FormatMotionMatchingBlendStack(*MMNode));
                    UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *BlendStackDebugLine);
                    AppendMotionMatchingCaptureLine(BlendStackDebugLine);

                    Info.LastSelectedAnim = Result.SelectedAnim.Get();
                    Info.LastSelectedTime = Result.SelectedTime;
                }
            }
            ++MotionMatchingNodeIndex;
        }
    }

    if (bCaptureMotionMatchingFrame)
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
        AppendMotionMatchingCaptureLine(StateDebugLine);

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
            AppendMotionMatchingCaptureLine(FallOffTrajectoryDebugLine);
        }
    }

}

UMotionMatchingAnimInstance::UMotionMatchingAnimInstance()
{
    bUseMultiThreadedAnimationUpdate = true;
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

        case ELocomotionState::TurnInPlace:
            CurrentActivePoseSearchDatabase = TurnInPlaceDatabase;
            break;

        case ELocomotionState::Start:
            if (bSprinting)
            {
                CurrentActivePoseSearchDatabase = SprintStartDatabase;
            }
            else
            {
                CurrentActivePoseSearchDatabase = bSimulated ? StartDatabaseRemote : StartDatabase;
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
                    CurrentActivePoseSearchDatabase = bSimulated ? LocomotionTransitionDatabaseRemote : LocomotionTransitionDatabase;
                }
                else
                {
                    CurrentActivePoseSearchDatabase = bSimulated ? LocomotionDatabaseRemote : LocomotionDatabase;
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
                    TArray<FTransformTrajectorySample>& Samples = ThreadSafeData.MovementData.Trajectory.Samples;
                    FQuat ActorQuat = CachedBasePlayer->GetActorQuat();
                    ELocomotionState State = CachedLocomotionStateComponent->CurrentState;

                    if (State == ELocomotionState::InAir && CachedLocomotionStateComponent->bIsFallOffStart)
                    {
                        ApplyFallingPredictionToTrajectory(ThreadSafeData.MovementData.Trajectory, *CachedBasePlayer);
                    }
                    else if (State == ELocomotionState::TurnInPlace)
                    {
                        // 제자리 회전 상태: 미래의 바라보는 방향(Facing)을 마우스(컨트롤러) 회전 방향으로 블렌딩하여 모션 매칭이 제자리 회전 애니메이션을 찾아내도록 유도
                        FQuat TargetQuat = CachedBasePlayer->GetControlRotation().Quaternion();
                        for (FTransformTrajectorySample& Sample : Samples)
                        {
                            if (Sample.TimeInSeconds > 0.f)
                            {
                                float Alpha = FMath::Clamp(Sample.TimeInSeconds / 0.8f, 0.f, 1.f);
                                FQuat BlendedQuat = FQuat::Slerp(ActorQuat, TargetQuat, Alpha);
                                FTransform TempTransform = Sample.GetTransform();
                                TempTransform.SetRotation(BlendedQuat);
                                Sample.SetTransform(TempTransform);
                            }
                        }
                    }
                    else if (State == ELocomotionState::Landing
                        && CachedLocomotionStateComponent->bLandWasMoving
                        && !CachedLocomotionStateComponent->LandMoveDirection.IsNearlyZero())
                    {
                        // Keep the landing query aligned with the movement direction captured at impact.
                        // Live input/trajectory can change on the landing frame, especially for back/strafe falls.
                        const FVector2D LocalDirection = CachedLocomotionStateComponent->LandMoveDirection.GetSafeNormal();
                        const FVector WorldDirection =
                            CachedBasePlayer->GetActorForwardVector() * LocalDirection.Y +
                            CachedBasePlayer->GetActorRightVector() * LocalDirection.X;
                        const FVector ActorLocation = CachedBasePlayer->GetActorLocation();
                        const float QuerySpeed = FMath::Max(
                            CachedLocomotionStateComponent->LandStartGroundSpeed,
                            CachedLocomotionStateComponent->IdleSpeedThreshold);

                        for (FTransformTrajectorySample& Sample : Samples)
                        {
                            if (Sample.TimeInSeconds > 0.f)
                            {
                                FTransform SampleTransform = Sample.GetTransform();
                                FVector SampleLocation = SampleTransform.GetLocation();
                                const FVector LockedLocation = ActorLocation + WorldDirection * QuerySpeed * Sample.TimeInSeconds;
                                SampleLocation.X = LockedLocation.X;
                                SampleLocation.Y = LockedLocation.Y;
                                SampleTransform.SetLocation(SampleLocation);
                                SampleTransform.SetRotation(ActorQuat);
                                Sample.SetTransform(SampleTransform);
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

    // 4. Push variables safely to the proxy
    FMotionMatchingAnimInstanceProxy& MyProxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
    MyProxy.ThreadSafeData = ThreadSafeData;
    MyProxy.CurrentActivePoseSearchDatabase = CurrentActivePoseSearchDatabase;

    // Debug Logging for Local Player Character
    if (CVarMotionMatchingDebugLogging.GetValueOnGameThread() > 0 && CachedBasePlayer && CachedBasePlayer->IsLocallyControlled())
    {
        StateLogTimer += DeltaSeconds;

        FName CurrentDatabaseName = CurrentActivePoseSearchDatabase ? CurrentActivePoseSearchDatabase->GetFName() : NAME_None;

        bool bChanged = (CachedLocomotionStateComponent && CachedLocomotionStateComponent->CurrentState != LastState) || (CurrentDatabaseName != LastDatabaseName);

        if (bChanged)
        {
            FString OldStateStr = StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)LastState);
            FString NewStateStr = CachedLocomotionStateComponent ? StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)CachedLocomotionStateComponent->CurrentState) : TEXT("None");
            UE_LOG(LogTemp, Log, TEXT("[MM_STATE_CHANGE] State changed: %s -> %s | Database: %s"),
                *OldStateStr, *NewStateStr, *CurrentDatabaseName.ToString());

            if (CachedLocomotionStateComponent)
            {
                const FString PsdDebugLine = FString::Printf(
                    TEXT("[MMCAP_PSD] SelectPSD PrevState=%s State=%s Database=%s Jump=%d FallOff=%d Landing=%d LandingRequested=%d Heavy=%d LandMoving=%d SprintLand=%d LandTime=%.3f LandSpeed=%.1f FallSpeed=%.1f Input=(R=%.2f,F=%.2f)"),
                    *OldStateStr,
                    *NewStateStr,
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
                    CachedLocomotionStateComponent->CachedMoveInput.Y);
                UE_LOG(LogMotionMatchingCapture, Display, TEXT("%s"), *PsdDebugLine);
                AppendMotionMatchingCaptureLine(PsdDebugLine);

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
                    AppendMotionMatchingCaptureLine(LandToStartDebugLine);
                }
            }
        }

        if (bChanged || StateLogTimer >= 0.2f)
        {
            StateLogTimer = 0.0f;
            if (CachedLocomotionStateComponent)
            {
                LastState = CachedLocomotionStateComponent->CurrentState;
            }
            LastDatabaseName = CurrentDatabaseName;

            FVector2D MoveInput = CachedBasePlayer->CachedMoveInput;
            float ControlYaw = CachedBasePlayer->GetControlRotation().Yaw;
            float ActorYaw = CachedBasePlayer->GetActorRotation().Yaw;
            float YawDelta = FMath::FindDeltaAngleDegrees(ActorYaw, ControlYaw);

            bool bOrient = false;
            bool bUseControllerDesired = false;
            if (UCharacterMovementComponent* MoveComp = CachedBasePlayer->GetCharacterMovement())
            {
                bOrient = MoveComp->bOrientRotationToMovement;
                bUseControllerDesired = MoveComp->bUseControllerDesiredRotation;
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

            UE_LOG(LogTemp, Log, TEXT("[MM_DEBUG] LocalPlayer: Speed=%.1f, State=%s, Database=%s, Sprint=%s"),
                CachedLocomotionStateComponent ? CachedLocomotionStateComponent->GroundSpeed : 0.f,
                CachedLocomotionStateComponent ? *StaticEnum<ELocomotionState>()->GetNameStringByValue((int64)CachedLocomotionStateComponent->CurrentState) : TEXT("None"),
                *CurrentDatabaseName.ToString(),
                CachedBasePlayer->bIsSprinting ? TEXT("True") : TEXT("False"));

            UE_LOG(LogTemp, Log, TEXT("          Rotation: ControlYaw=%.1f, ActorYaw=%.1f, DeltaYaw=%.1f, bOrientToMovement=%s, bUseControllerDesired=%s"),
                ControlYaw, ActorYaw, YawDelta,
                bOrient ? TEXT("True") : TEXT("False"),
                bUseControllerDesired ? TEXT("True") : TEXT("False"));

            UE_LOG(LogTemp, Log, TEXT("          Trajectory: %s %s %s %s %s"),
                *TrajHistoryStr, *TrajCurrentStr, *TrajFuture1Str, *TrajFuture2Str, *TrajFuture3Str);

            UAnimMontage* ActiveMontage = GetCurrentActiveMontage();
            UE_LOG(LogMotionMatchingCapture, Display,
                TEXT("[MMCAP_GRAPH] AnimInstance=%s ActiveMontage=%s MontagePosition=%.3f MontagePlaying=%d"),
                *GetName(),
                *GetNameSafe(ActiveMontage),
                ActiveMontage ? Montage_GetPosition(ActiveMontage) : 0.f,
                ActiveMontage && Montage_IsPlaying(ActiveMontage) ? 1 : 0);
        }
    }
}

UPoseSearchDatabase* UMotionMatchingAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
    return CurrentActivePoseSearchDatabase;
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
        return false;
    }

    MotionMatchingUpdateAccumulator = 0.0f;
    return true;
}
