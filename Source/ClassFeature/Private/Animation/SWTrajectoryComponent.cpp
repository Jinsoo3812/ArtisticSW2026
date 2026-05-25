#include "Animation/SWTrajectoryComponent.h"
#include "UObject/UnrealType.h"

USWTrajectoryComponent::USWTrajectoryComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void USWTrajectoryComponent::ResetTrajectoryHistory()
{
    // Clear the trajectory struct property using reflection
    TArray<FName> PropertyNames = { FName("Trajectory"), FName("QueryTrajectory") };
    for (const FName& PropName : PropertyNames)
    {
        if (FProperty* Prop = GetClass()->FindPropertyByName(PropName))
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                void* PropPtr = StructProp->ContainerPtrToValuePtr<void>(this);
                if (PropPtr)
                {
                    StructProp->InitializeValue(PropPtr);
                }
            }
        }
    }
}
