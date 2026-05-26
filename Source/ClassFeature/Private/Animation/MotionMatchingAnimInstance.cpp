#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "CharacterTrajectoryComponent.h"
#include "ChooserFunctionLibrary.h"
#include "Chooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

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
    FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InGroupRelevancyName);

    UAnimInstance* AnimInstanceObj = Cast<UAnimInstance>(GetAnimInstanceObject());
    if (!AnimInstanceObj) return;

    if (!bNodesCached)
    {
        CacheNodes(AnimInstanceObj);
    }

    // 1. Update Motion Matching Nodes database references
    for (const FCachedMotionMatchingNodeInfo& Info : CachedMMNodes)
    {
        if (Info.NodeProperty && Info.DatabaseProperty)
        {
            FAnimNode_MotionMatching* MMNode = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
            if (MMNode)
            {
                Info.DatabaseProperty->SetObjectPropertyValue_InContainer(MMNode, CurrentActivePoseSearchDatabase);
            }
        }
    }

    // 2. Update History Collector Nodes trajectories
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
}

UMotionMatchingAnimInstance::UMotionMatchingAnimInstance()
{
    bUseMultiThreadedAnimationUpdate = true;

    bChooserIsIdle = false;
    bChooserIsRunStart = false;
    bChooserIsRunStartRemote = false;
    bChooserIsSprintStart = false;
    bChooserIsRunLocomotion = false;
    bChooserIsRunLocomotionRemote = false;
    bChooserIsSprintLocomotion = false;
    bChooserIsRunStop = false;
    bChooserIsSprintStop = false;
    bChooserIsJumpStart = false;
    bChooserIsInAir = false;
    bChooserIsLandingHeavy = false;
    bChooserIsLandingLight = false;
    bChooserIsRunLandHeavy = false;
    bChooserIsRunLandLight = false;
    bChooserIsSprintLandHeavy = false;
    bChooserIsSprintLandLight = false;
    bChooserIsFallOffStart = false;
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

    // 1. Reset and Pre-process state checks for Chooser Table (mutually exclusive)
    bChooserIsIdle = false;
    bChooserIsRunStart = false;
    bChooserIsRunStartRemote = false;
    bChooserIsSprintStart = false;
    bChooserIsRunLocomotion = false;
    bChooserIsRunLocomotionRemote = false;
    bChooserIsSprintLocomotion = false;
    bChooserIsRunStop = false;
    bChooserIsSprintStop = false;
    bChooserIsJumpStart = false;
    bChooserIsInAir = false;
    bChooserIsLandingHeavy = false;
    bChooserIsLandingLight = false;
    bChooserIsRunLandHeavy = false;
    bChooserIsRunLandLight = false;
    bChooserIsSprintLandHeavy = false;
    bChooserIsSprintLandLight = false;
    bChooserIsFallOffStart = false;

    if (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Landing)
    {
        const bool bSprintLand = CachedLocomotionStateComponent->bLandWasSprinting;
        const bool bHeavyLand = CachedLocomotionStateComponent->bUseHeavyLand;

        if (bSprintLand && bHeavyLand)
        {
            bChooserIsSprintLandHeavy = true;
        }
        else if (bSprintLand)
        {
            bChooserIsSprintLandLight = true;
        }
        else if (bHeavyLand)
        {
            bChooserIsRunLandHeavy = true;
        }
        else
        {
            bChooserIsRunLandLight = true;
        }
    }
    else if (CachedLocomotionStateComponent->bIsFallOffStart)
    {
        bChooserIsFallOffStart = true;
    }
    else
    {
        bool bSprinting = CachedLocomotionStateComponent->bIsSprinting;

        switch (CachedLocomotionStateComponent->CurrentState)
        {
        case ELocomotionState::Idle:
            bChooserIsIdle = true;
            break;
        case ELocomotionState::Start:
            if (bSprinting)
            {
                bChooserIsSprintStart = true;
            }
            else
            {
                if (CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy)
                {
                    bChooserIsRunStartRemote = true;
                }
                else
                {
                    bChooserIsRunStart = true;
                }
            }
            break;
        case ELocomotionState::Locomotion:
            if (bSprinting)
            {
                bChooserIsSprintLocomotion = true;
            }
            else
            {
                if (CachedBasePlayer->GetLocalRole() == ROLE_SimulatedProxy)
                {
                    bChooserIsRunLocomotionRemote = true;
                }
                else
                {
                    bChooserIsRunLocomotion = true;
                }
            }
            break;
        case ELocomotionState::Stop:
            if (bSprinting) bChooserIsSprintStop = true;
            else bChooserIsRunStop = true;
            break;
        case ELocomotionState::InAir:
            if (CachedLocomotionStateComponent->bIsJumping)
            {
                bChooserIsJumpStart = true;
            }
            else
            {
                bChooserIsInAir = true;
            }
            break;
        default:
            bChooserIsIdle = true;
            break;
        }
    }

    // 2. Evaluate Chooser Table to select the active UPoseSearchDatabase
    if (ChooserTable)
    {
        UObject* SelectedAsset = UChooserFunctionLibrary::EvaluateChooser(this, ChooserTable, UPoseSearchDatabase::StaticClass());
        CurrentActivePoseSearchDatabase = Cast<UPoseSearchDatabase>(SelectedAsset);
    }
    else
    {
        CurrentActivePoseSearchDatabase = nullptr;
    }

    // 3. Pack data into thread-safe struct
    FAnimThreadSafeData ThreadSafeData;

    // Movement Data
    ThreadSafeData.MovementData.Velocity = CachedLocomotionStateComponent->Velocity;
    ThreadSafeData.MovementData.Acceleration = CachedLocomotionStateComponent->Acceleration;
    
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
            }
        }
    }

    // Input Data
    ThreadSafeData.InputData.MoveInputSize = CachedLocomotionStateComponent->MoveInputSize;
    ThreadSafeData.InputData.bHasMoveInput = CachedLocomotionStateComponent->bHasMoveInput;
    ThreadSafeData.InputData.bSharpTurnRequested = CachedLocomotionStateComponent->bSharpTurnRequested;

    // Ground Data
    ThreadSafeData.GroundData.bStartRequested = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start);
    ThreadSafeData.GroundData.bStopRequested = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop);
    ThreadSafeData.GroundData.GroundMotionMode = static_cast<uint8>(CachedLocomotionStateComponent->CurrentState);

    // Air Data
    ThreadSafeData.AirData.bIsInAir = CachedLocomotionStateComponent->bIsInAir;
    ThreadSafeData.AirData.bIsJumping = CachedLocomotionStateComponent->bIsJumping;

    // Landing Data
    ThreadSafeData.LandingData.bIsLanding = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Landing);
    ThreadSafeData.LandingData.bUseHeavyLand = CachedLocomotionStateComponent->bUseHeavyLand;
    ThreadSafeData.LandingData.LastFallSpeed = CachedLocomotionStateComponent->LastFallSpeed;

    // 4. Push variables safely to the proxy
    FMotionMatchingAnimInstanceProxy& MyProxy = GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
    MyProxy.ThreadSafeData = ThreadSafeData;
    MyProxy.CurrentActivePoseSearchDatabase = CurrentActivePoseSearchDatabase;
}

UPoseSearchDatabase* UMotionMatchingAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
    return CurrentActivePoseSearchDatabase;
}
