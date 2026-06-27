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

    const FQuat QueryQuat = MakeMotionMatchingQueryRotation(CharacterOwner);

    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        // simulated proxy媛 ?吏곸씠怨??덉쑝誘濡? 誘몃옒 ?덉륫 ?섑뵆?ㅼ쓽 Facing???≫꽣 ?뚯쟾???뺣젹?쒗궡 (Strafe 紐⑤뱶)
        if (Trajectory.Samples[Index].TimeInSeconds < -UE_KINDA_SMALL_NUMBER)
        {
            continue;
        }

        FTransform RepairedTransform = Trajectory.Samples[Index].GetTransform();
        RepairedTransform.SetRotation(QueryQuat);
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
    if (State != ELocomotionState::Start && State != ELocomotionState::Locomotion)
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

    const FVector ActorLocation = CharacterOwner.GetActorLocation();
    const FQuat QueryQuat = MakeMotionMatchingQueryRotation(CharacterOwner);

    bool bRepairedAnySample = false;
    for (FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        if (Sample.TimeInSeconds <= UE_KINDA_SMALL_NUMBER)
        {
            continue;
        }

        FTransform SampleTransform = Sample.GetTransform();
        const FVector LockedLocation = ActorLocation + WorldDirection * QuerySpeed * Sample.TimeInSeconds;
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

