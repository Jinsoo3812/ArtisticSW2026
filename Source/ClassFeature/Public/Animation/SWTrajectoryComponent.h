#pragma once

#include "CoreMinimal.h"
#include "CharacterTrajectoryComponent.h"
#include "SWTrajectoryComponent.generated.h"

class ACharacter;

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API USWTrajectoryComponent : public UCharacterTrajectoryComponent
{
    GENERATED_BODY()

public:
    USWTrajectoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void InitializeComponent() override;

    UFUNCTION(BlueprintCallable, Category = "Locomotion|Trajectory")
    void ResetTrajectoryHistory();

    UFUNCTION(BlueprintPure, Category = "Locomotion|Trajectory")
    const FTransformTrajectory& GetTrajectory() const { return Trajectory; }

    UFUNCTION(BlueprintPure, Category = "Locomotion|Trajectory")
    const FTrajectorySamplingData& GetSamplingData() const { return SamplingData; }

    UFUNCTION(BlueprintPure, Category = "Locomotion|Trajectory")
    const FCharacterTrajectoryData& GetCharacterTrajectoryData() const { return CharacterTrajectoryData; }

    void UpdateTrajectoryState(float DeltaTime);

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Smoothing")
    bool bEnableTrajectorySmoothing = false;

    /** Interpolation speed coefficient (alpha = Clamp(DeltaTime * TrajectorySmoothingSpeed, 0, 1)) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Smoothing", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableTrajectorySmoothing"))
    float TrajectorySmoothingSpeed = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Correction")
    bool bEnableSpeedChangeCorrection = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Correction")
    bool bEnableRemoteFacingRepair = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Correction", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableRemoteFacingRepair"))
    float RemoteFacingRepairMinSpeed = 80.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Trajectory|Correction", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableRemoteFacingRepair"))
    float RemoteFacingRepairMaxYawDelta = 35.0f;

private:
    void EnsureTrajectoryBuffers();
    void ApplyTrajectorySmoothing(float DeltaTime);
    void RepairRemoteTrajectoryFacing(const ACharacter& CharacterOwner);
    void RepairRemoteTrajectoryPrediction(const ACharacter& CharacterOwner);
    void ScaleTrajectoryHistory(float ScaleRatio);

    UPROPERTY(Transient)
    FTransformTrajectory PreviousFilteredTrajectory;

    UPROPERTY(Transient)
    float LastMaxWalkSpeed = 0.0f;

    FVector PreviousAcceleration = FVector::ZeroVector;
    FVector LastReplicatedVelocity = FVector::ZeroVector;
};

