#pragma once

#include "Animation/AnimInstance.h"
#include "SWNpcAnimInstance.generated.h"

/** Minimal ground locomotion data for NPCs using a speed blend space. */
UCLASS(Transient, Blueprintable)
class CLASSFEATURE_API USWNpcAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC|Locomotion")
    float NpcGroundSpeed = 0.0f;

    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
