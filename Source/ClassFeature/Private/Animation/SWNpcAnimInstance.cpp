#include "Animation/SWNpcAnimInstance.h"
#include "GameFramework/Actor.h"

void USWNpcAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const AActor* Owner = GetOwningActor();
    NpcGroundSpeed = Owner ? Owner->GetVelocity().Size2D() : 0.0f;
}
