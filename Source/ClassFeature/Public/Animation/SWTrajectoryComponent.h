#pragma once

#include "CoreMinimal.h"
#include "CharacterTrajectoryComponent.h"
#include "SWTrajectoryComponent.generated.h"

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API USWTrajectoryComponent : public UCharacterTrajectoryComponent
{
    GENERATED_BODY()

public:
    USWTrajectoryComponent(const FObjectInitializer& ObjectInitializer);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Clears/resets the trajectory history using reflection
    UFUNCTION(BlueprintCallable, Category = "Locomotion|Trajectory")
    void ResetTrajectoryHistory();

private:
    FVector PreviousAcceleration = FVector::ZeroVector;
    FVector LastReplicatedVelocity = FVector::ZeroVector;
};
