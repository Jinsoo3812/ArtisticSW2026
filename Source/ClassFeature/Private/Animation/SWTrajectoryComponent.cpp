#include "Animation/SWTrajectoryComponent.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

USWTrajectoryComponent::USWTrajectoryComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
}

void USWTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* MovementComponent = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;
    if (!OwnerCharacter || !MovementComponent)
    {
        return;
    }

    if (OwnerCharacter->GetLocalRole() == ROLE_SimulatedProxy)
    {
        FVector ReplicatedAcceleration = FVector::ZeroVector;
        if (DeltaTime > 0.f)
        {
            ReplicatedAcceleration = (OwnerCharacter->GetVelocity() - LastReplicatedVelocity) / DeltaTime;
        }
        LastReplicatedVelocity = OwnerCharacter->GetVelocity();

        if (FProperty* AccelProp = MovementComponent->GetClass()->FindPropertyByName(TEXT("Acceleration")))
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(AccelProp))
            {
                FVector* AccelPtr = StructProp->ContainerPtrToValuePtr<FVector>(MovementComponent);
                if (AccelPtr)
                {
                    *AccelPtr = ReplicatedAcceleration;
                }
            }
        }
    }

    const FVector CurrentAcceleration = MovementComponent->GetCurrentAcceleration();
    if (CurrentAcceleration.IsNearlyZero() && !PreviousAcceleration.IsNearlyZero())
    {
        ResetTrajectoryHistory();
    }
    PreviousAcceleration = CurrentAcceleration;
}

void USWTrajectoryComponent::ResetTrajectoryHistory()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    FVector ActorLocation = Owner->GetActorLocation();
    FQuat ActorQuat = Owner->GetActorQuat();

    // Reset/Modify the trajectory struct property using reflection
    TArray<FName> PropertyNames = { FName("Trajectory"), FName("QueryTrajectory") };
    for (const FName& PropName : PropertyNames)
    {
        if (FProperty* Prop = GetClass()->FindPropertyByName(PropName))
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                UScriptStruct* Struct = StructProp->Struct;
                if (!Struct) continue;

                void* PropPtr = StructProp->ContainerPtrToValuePtr<void>(this);
                if (!PropPtr) continue;

                FArrayProperty* SamplesProp = CastField<FArrayProperty>(Struct->FindPropertyByName(TEXT("Samples")));
                if (SamplesProp)
                {
                    FScriptArrayHelper ArrayHelper(SamplesProp, SamplesProp->ContainerPtrToValuePtr<void>(PropPtr));
                    
                    // Ensure the array has a safe minimum size (e.g. 30 samples) to prevent assertion crashes
                    if (ArrayHelper.Num() < 30)
                    {
                        ArrayHelper.Resize(30);
                    }

                    // Get properties of the trajectory sample elements.
                    UScriptStruct* ElementStruct = nullptr;
                    if (FStructProperty* InnerStructProp = CastField<FStructProperty>(SamplesProp->Inner))
                    {
                        ElementStruct = InnerStructProp->Struct;
                    }

                    if (ElementStruct)
                    {
                        FProperty* PosProp = ElementStruct->FindPropertyByName(TEXT("Position"));
                        FProperty* FacingProp = ElementStruct->FindPropertyByName(TEXT("Facing"));
                        if (!FacingProp)
                        {
                            FacingProp = ElementStruct->FindPropertyByName(TEXT("Rotation"));
                        }

                        // Loop through all samples and set them to the current position/facing with zero speed
                        for (int32 i = 0; i < ArrayHelper.Num(); ++i)
                        {
                            uint8* ElementPtr = ArrayHelper.GetRawPtr(i);
                            if (PosProp)
                            {
                                if (FStructProperty* PosStructProp = CastField<FStructProperty>(PosProp))
                                {
                                    if (PosStructProp->Struct == TBaseStructure<FVector>::Get())
                                    {
                                        PosStructProp->CopyCompleteValue(PosProp->ContainerPtrToValuePtr<void>(ElementPtr), &ActorLocation);
                                    }
                                }
                            }
                            if (FacingProp)
                            {
                                if (FStructProperty* FacingStructProp = CastField<FStructProperty>(FacingProp))
                                {
                                    if (FacingStructProp->Struct == TBaseStructure<FQuat>::Get())
                                    {
                                        FacingStructProp->CopyCompleteValue(FacingProp->ContainerPtrToValuePtr<void>(ElementPtr), &ActorQuat);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
