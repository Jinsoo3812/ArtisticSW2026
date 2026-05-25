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

FMotionMatchingAnimInstanceProxy::FMotionMatchingAnimInstanceProxy()
    : FAnimInstanceProxy()
{
}

FMotionMatchingAnimInstanceProxy::FMotionMatchingAnimInstanceProxy(UAnimInstance* InAnimInstance)
    : FAnimInstanceProxy(InAnimInstance)
{
}

void FMotionMatchingAnimInstanceProxy::UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InGroupRelevancyName)
{
    FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InGroupRelevancyName);

    UAnimInstance* AnimInstanceObj = Cast<UAnimInstance>(GetAnimInstanceObject());
    if (!AnimInstanceObj) return;

    // Traverse the AnimInstance properties using reflection to locate MM nodes
    // and safely update them on the worker thread.
    for (TFieldIterator<FProperty> PropIt(AnimInstanceObj->GetClass()); PropIt; ++PropIt)
    {
        FStructProperty* StructProp = CastField<FStructProperty>(*PropIt);
        if (!StructProp || !StructProp->Struct) continue;

        if (StructProp->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))
        {
            FAnimNode_MotionMatching* MMNode = StructProp->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(AnimInstanceObj);
            if (MMNode)
            {
                // Write to private Database property using reflection
                if (FProperty* DbProp = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("Database")))
                {
                    if (FObjectProperty* ObjProp = CastField<FObjectProperty>(DbProp))
                    {
                        ObjProp->SetObjectPropertyValue_InContainer(MMNode, CurrentActivePoseSearchDatabase);
                    }
                }
            }
        }
        else if (StructProp->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector::StaticStruct()))
        {
            FAnimNode_PoseSearchHistoryCollector* HistoryNode = StructProp->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(AnimInstanceObj);
            if (HistoryNode)
            {
                // Set using reflection to support both deprecated Trajectory and new TransformTrajectory
                TArray<FName> HistoryPropNames = { FName("TransformTrajectory"), FName("Trajectory") };
                for (const FName& PropName : HistoryPropNames)
                {
                    if (FProperty* HistoryProp = StructProp->Struct->FindPropertyByName(PropName))
                    {
                        if (FStructProperty* HistoryStructProp = CastField<FStructProperty>(HistoryProp))
                        {
                            if (HistoryStructProp->Struct)
                            {
                                void* HistoryPropPtr = HistoryStructProp->ContainerPtrToValuePtr<void>(HistoryNode);
                                if (HistoryPropPtr)
                                {
                                    HistoryStructProp->Struct->CopyScriptStruct(HistoryPropPtr, &ThreadSafeData.MovementData.Trajectory);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

UMotionMatchingAnimInstance::UMotionMatchingAnimInstance()
{
    bUseMultiThreadedAnimationUpdate = true;

    bChooserUseRunStart = false;
    bChooserUseRunStop = false;
    bChooserIsInAir = false;
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
    }
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

    // 1. Pre-process state priority checks for Chooser Table
    bChooserUseRunStart = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Start);
    bChooserUseRunStop = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::Stop);
    bChooserIsInAir = (CachedLocomotionStateComponent->CurrentState == ELocomotionState::InAir);

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
        TArray<FName> TrajPropNames = { FName("Trajectory"), FName("QueryTrajectory") };
        for (const FName& PropName : TrajPropNames)
        {
            if (FProperty* Prop = TrajectoryComp->GetClass()->FindPropertyByName(PropName))
            {
                if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                {
                    void* PropPtr = StructProp->ContainerPtrToValuePtr<void>(TrajectoryComp);
                    if (PropPtr)
                    {
                        StructProp->Struct->CopyScriptStruct(&ThreadSafeData.MovementData.Trajectory, PropPtr);
                        break;
                    }
                }
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
