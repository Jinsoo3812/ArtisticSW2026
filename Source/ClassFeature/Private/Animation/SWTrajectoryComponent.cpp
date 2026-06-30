#include "Animation/SWTrajectoryComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionTrajectoryLibrary.h"
#include "Async/ParallelFor.h"
#include "Animation/LocomotionAnimStateComponent.h"

namespace
{
    FQuat MakeMotionMatchingQueryRotation(const ACharacter& CharacterOwner)
    {
        return FQuat(FRotator(0.f, -90.f, 0.f)) * CharacterOwner.GetActorQuat();
    }

    int32 FindPresentTrajectorySampleIndex(const FTransformTrajectory& Trajectory)
    {
        int32 PresentSampleIndex = INDEX_NONE;
        float SmallestAbsTime = TNumericLimits<float>::Max();

        for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
        {
            const float AbsTime = FMath::Abs(Trajectory.Samples[Index].TimeInSeconds);
            if (AbsTime < SmallestAbsTime)
            {
                SmallestAbsTime = AbsTime;
                PresentSampleIndex = Index;
            }
        }

        return PresentSampleIndex;
    }

    FVector GetHorizontalSampleDelta(const FTransformTrajectory& Trajectory, int32 FromIndex, int32 ToIndex)
    {
        if (!Trajectory.Samples.IsValidIndex(FromIndex) || !Trajectory.Samples.IsValidIndex(ToIndex))
        {
            return FVector::ZeroVector;
        }

        FVector Delta = Trajectory.Samples[ToIndex].GetTransform().GetLocation() -
            Trajectory.Samples[FromIndex].GetTransform().GetLocation();
        Delta.Z = 0.f;
        return Delta;
    }

    FVector ResolveTrajectorySampleDirection(const FTransformTrajectory& Trajectory, int32 SampleIndex, const FVector& FallbackDirection)
    {
        FVector Direction = GetHorizontalSampleDelta(Trajectory, SampleIndex, SampleIndex + 1);
        if (Direction.IsNearlyZero())
        {
            Direction = GetHorizontalSampleDelta(Trajectory, SampleIndex - 1, SampleIndex);
        }

        return Direction.IsNearlyZero() ? FallbackDirection : Direction.GetSafeNormal();
    }
}

USWTrajectoryComponent::USWTrajectoryComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USWTrajectoryComponent::InitializeComponent()
{
    Super::InitializeComponent();
    EnsureTrajectoryBuffers();
}

void USWTrajectoryComponent::ResetTrajectoryHistory()
{
    EnsureTrajectoryBuffers();

    ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
    if (!CharacterOwner)
    {
        Trajectory.Samples.Reset();
        TranslationHistory.Reset();
        LastUpdateFrameNumber = 0;
        return;
    }

    SamplingData.Init();
    CharacterTrajectoryData.UpdateDataFromCharacter(0.0f, CharacterOwner);
    Trajectory.Samples.Reset();
    TranslationHistory.Reset();

    FMotionTrajectoryLibrary::InitTrajectorySamples(
        Trajectory,
        SamplingData,
        CharacterOwner->GetActorLocation(),
        CharacterOwner->GetActorQuat());

    TranslationHistory.SetNumZeroed(SamplingData.NumHistorySamples);
    LastUpdateFrameNumber = GFrameCounter;

    PreviousFilteredTrajectory = Trajectory;
    if (CharacterOwner->GetCharacterMovement())
    {
        LastMaxWalkSpeed = CharacterOwner->GetCharacterMovement()->MaxWalkSpeed;
    }
    else
    {
        LastMaxWalkSpeed = 0.0f;
    }
}

void USWTrajectoryComponent::EnsureTrajectoryBuffers()
{
    SamplingData.Init();

    const int32 RequiredTrajectorySamples = SamplingData.NumHistorySamples + 1 + SamplingData.NumPredictionSamples;
    Trajectory.Samples.Reserve(RequiredTrajectorySamples);
    TranslationHistory.Reserve(SamplingData.NumHistorySamples);
}

void USWTrajectoryComponent::UpdateTrajectoryState(float DeltaTime)
{
    ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
    if (!CharacterOwner)
    {
        return;
    }

    if (IsRegistered())
    {
        Super::TickComponent(DeltaTime, ELevelTick::LEVELTICK_All, nullptr);
    }

    if (bEnableSpeedChangeCorrection)
    {
        if (const UCharacterMovementComponent* MoveComp = CharacterOwner->GetCharacterMovement())
        {
            float CurrentMaxWalkSpeed = MoveComp->MaxWalkSpeed;
            if (LastMaxWalkSpeed > 0.0f && !FMath::IsNearlyEqual(CurrentMaxWalkSpeed, LastMaxWalkSpeed))
            {
                float SpeedScaleRatio = CurrentMaxWalkSpeed / LastMaxWalkSpeed;
                ScaleTrajectoryHistory(SpeedScaleRatio);
            }
            LastMaxWalkSpeed = CurrentMaxWalkSpeed;
        }
    }

    const bool bSimulatedProxy = CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;

    if (bEnableTrajectorySmoothing && bSimulatedProxy)
    {
        ApplyTrajectorySmoothing(DeltaTime);
    }
    else if (bSimulatedProxy)
    {
        PreviousFilteredTrajectory = Trajectory;
    }

    if (bSimulatedProxy && bEnableRemoteFacingRepair)
    {
        RepairRemoteTrajectoryFacing(*CharacterOwner);
        RepairRemoteTrajectoryPrediction(*CharacterOwner);
    }

    if (UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement())
    {
        const FVector CurrentAcceleration = MovementComponent->GetCurrentAcceleration();
        const FVector CurrentVelocity = MovementComponent->Velocity;

        const bool bJustStoppedInput = CurrentAcceleration.IsNearlyZero() && !PreviousAcceleration.IsNearlyZero();
        const bool bIsNearlyStopped = CurrentVelocity.SizeSquared2D() < FMath::Square(20.f);

        if (bJustStoppedInput && bIsNearlyStopped)
        {
            ResetTrajectoryHistory();
        }
        PreviousAcceleration = CurrentAcceleration;
    }
}
void USWTrajectoryComponent::ScaleTrajectoryHistory(float ScaleRatio)
{
    ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
    if (!CharacterOwner || Trajectory.Samples.IsEmpty() || ScaleRatio <= 0.0f)
    {
        return;
    }

    FTransform ActorTransform = CharacterOwner->GetActorTransform();

    for (FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        if (Sample.TimeInSeconds < -UE_KINDA_SMALL_NUMBER)
        {
            FTransform LocalTransform = Sample.GetTransform().GetRelativeTransform(ActorTransform);

            FVector LocalPosition = LocalTransform.GetLocation();
            LocalPosition.X *= ScaleRatio;
            LocalPosition.Y *= ScaleRatio;
            LocalTransform.SetLocation(LocalPosition);

            Sample.SetTransform(LocalTransform * ActorTransform);
        }
    }

    PreviousFilteredTrajectory = Trajectory;
}

void USWTrajectoryComponent::ApplyTrajectorySmoothing(float DeltaTime)
{
    if (Trajectory.Samples.IsEmpty() || DeltaTime <= 0.0f)
    {
        return;
    }

    ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
    if (!CharacterOwner)
    {
        return;
    }

    if (PreviousFilteredTrajectory.Samples.Num() != Trajectory.Samples.Num())
    {
        PreviousFilteredTrajectory = Trajectory;
        return;
    }

    FTransform ActorTransform = CharacterOwner->GetActorTransform();
    float Alpha = FMath::Clamp(DeltaTime * TrajectorySmoothingSpeed, 0.0f, 1.0f);

    const int32 NumSamples = Trajectory.Samples.Num();
    constexpr int32 ParallelSmoothingSampleThreshold = 64;

    auto SmoothingLogic = [&](int32 i)
    {
        FTransformTrajectorySample& CurrentSample = Trajectory.Samples[i];
        const FTransformTrajectorySample& PrevSample = PreviousFilteredTrajectory.Samples[i];

        FTransform LocalCurrent = CurrentSample.GetTransform().GetRelativeTransform(ActorTransform);
        FTransform LocalPrev = PrevSample.GetTransform().GetRelativeTransform(ActorTransform);

        // 濡쒖뺄 ?ㅽ럹?댁뒪 蹂닿컙
        const FVector LocalPos = FMath::Lerp(LocalPrev.GetLocation(), LocalCurrent.GetLocation(), Alpha);
        const FQuat LocalRot = FQuat::FastLerp(LocalPrev.GetRotation(), LocalCurrent.GetRotation(), Alpha).GetNormalized();

        FTransform LocalSmoothed(LocalRot, LocalPos, FVector::OneVector);
        CurrentSample.SetTransform(LocalSmoothed * ActorTransform);
    };

    if (NumSamples >= ParallelSmoothingSampleThreshold)
    {
        ParallelFor(NumSamples, SmoothingLogic);
    }
    else
    {
        for (int32 i = 0; i < NumSamples; ++i)
        {
            SmoothingLogic(i);
        }
    }

    PreviousFilteredTrajectory = Trajectory;
}

void USWTrajectoryComponent::RepairRemoteTrajectoryFacing(const ACharacter& CharacterOwner)
{
    const int32 NumSamples = Trajectory.Samples.Num();
    if (NumSamples < 2)
    {
        return;
    }

    FVector HorizontalVelocity = CharacterOwner.GetVelocity();
    HorizontalVelocity.Z = 0.f;
    const float GroundSpeed = HorizontalVelocity.Size();
    if (GroundSpeed < RemoteFacingRepairMinSpeed)
    {
        return;
    }

    const FVector VelocityDirection = HorizontalVelocity / GroundSpeed;
    const float VelocityYaw = VelocityDirection.Rotation().Yaw;
    const float ActorVelocityYawDelta = FMath::Abs(
        FMath::FindDeltaAngleDegrees(CharacterOwner.GetActorRotation().Yaw, VelocityYaw));
    if (ActorVelocityYawDelta > RemoteFacingRepairMaxYawDelta)
    {
        return;
    }

    const int32 PresentSampleIndex = FindPresentTrajectorySampleIndex(Trajectory);
    if (PresentSampleIndex == INDEX_NONE)
    {
        return;
    }

    const FTransform PresentTransform = Trajectory.Samples[PresentSampleIndex].GetTransform();
    const FVector PresentDirectionSafe = ResolveTrajectorySampleDirection(Trajectory, PresentSampleIndex, VelocityDirection);
    if (PresentDirectionSafe.IsNearlyZero())
    {
        return;
    }

    const float PresentMoveYaw = PresentDirectionSafe.Rotation().Yaw;
    const float PresentFaceYaw = PresentTransform.GetRotation().Rotator().Yaw;
    const float FacingOffsetYaw = FMath::FindDeltaAngleDegrees(PresentMoveYaw, PresentFaceYaw);

    FVector LastValidDirection = PresentDirectionSafe;

    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        if (Trajectory.Samples[Index].TimeInSeconds < -UE_KINDA_SMALL_NUMBER)
        {
            continue;
        }

        LastValidDirection = ResolveTrajectorySampleDirection(Trajectory, Index, LastValidDirection);
        if (LastValidDirection.IsNearlyZero())
        {
            continue;
        }

        FTransform RepairedTransform = Trajectory.Samples[Index].GetTransform();
        const float RepairedFacingYaw = LastValidDirection.Rotation().Yaw + FacingOffsetYaw;
        RepairedTransform.SetRotation(FRotator(0.f, RepairedFacingYaw, 0.f).Quaternion());
        Trajectory.Samples[Index].SetTransform(RepairedTransform);
    }

    PreviousFilteredTrajectory = Trajectory;
}

void USWTrajectoryComponent::RepairRemoteTrajectoryPrediction(const ACharacter& CharacterOwner)
{
    const int32 NumSamples = Trajectory.Samples.Num();
    if (NumSamples < 2)
    {
        return;
    }

    const UCharacterMovementComponent* MovementComponent = CharacterOwner.GetCharacterMovement();
    if (!MovementComponent)
    {
        return;
    }

    const ULocomotionAnimStateComponent* StateComponent = CharacterOwner.FindComponentByClass<ULocomotionAnimStateComponent>();
    if (!StateComponent)
    {
        return;
    }

    const ELocomotionState State = StateComponent->CurrentState;
    if (State != ELocomotionState::Start && State != ELocomotionState::Locomotion && State != ELocomotionState::Stop)
    {
        return;
    }

    FVector HorizontalVelocity = MovementComponent->Velocity;
    HorizontalVelocity.Z = 0.f;

    FVector WorldDirection = FVector::ZeroVector;
    if (!StateComponent->CachedMoveInput.IsNearlyZero())
    {
        const FVector2D LocalInput = StateComponent->CachedMoveInput.GetSafeNormal();
        WorldDirection =
            CharacterOwner.GetActorForwardVector() * LocalInput.Y +
            CharacterOwner.GetActorRightVector() * LocalInput.X;
        WorldDirection.Z = 0.f;
    }

    if (WorldDirection.IsNearlyZero() && !HorizontalVelocity.IsNearlyZero())
    {
        WorldDirection = HorizontalVelocity.GetSafeNormal();
    }

    if (WorldDirection.IsNearlyZero())
    {
        return;
    }

    WorldDirection.Normalize();

    const float CurrentSpeed = HorizontalVelocity.Size();
    const float MaxWalkSpeed = MovementComponent->GetMaxSpeed();
    const float QuerySpeed = State == ELocomotionState::Start
        ? FMath::Max(CurrentSpeed, MaxWalkSpeed)
        : FMath::Max(CurrentSpeed, StateComponent->IdleSpeedThreshold);
    if (QuerySpeed <= StateComponent->IdleSpeedThreshold)
    {
        return;
    }

    float Deceleration = 0.f;
    if (State == ELocomotionState::Stop)
    {
        Deceleration = MovementComponent->BrakingDecelerationWalking;
        if (Deceleration <= 0.f)
        {
            Deceleration = 2048.f;
        }
    }

    const FVector ActorLocation = CharacterOwner.GetActorLocation();
    const FQuat QueryQuat = MakeMotionMatchingQueryRotation(CharacterOwner);

    bool bRepairedAnySample = false;
    for (FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        if (Sample.TimeInSeconds <= UE_KINDA_SMALL_NUMBER)
        {
            continue;
        }

        float t = Sample.TimeInSeconds;
        float Distance = 0.f;
        if (State == ELocomotionState::Stop)
        {
            float StopTime = QuerySpeed / Deceleration;
            if (t < StopTime)
            {
                Distance = QuerySpeed * t - 0.5f * Deceleration * t * t;
            }
            else
            {
                Distance = 0.5f * QuerySpeed * StopTime;
            }
        }
        else
        {
            Distance = QuerySpeed * t;
        }

        FTransform SampleTransform = Sample.GetTransform();
        const FVector LockedLocation = ActorLocation + WorldDirection * Distance;
        FVector SampleLocation = SampleTransform.GetLocation();
        SampleLocation.X = LockedLocation.X;
        SampleLocation.Y = LockedLocation.Y;
        SampleTransform.SetLocation(SampleLocation);
        SampleTransform.SetRotation(QueryQuat);
        Sample.SetTransform(SampleTransform);
        bRepairedAnySample = true;
    }

    if (bRepairedAnySample)
    {
        PreviousFilteredTrajectory = Trajectory;
    }
}

