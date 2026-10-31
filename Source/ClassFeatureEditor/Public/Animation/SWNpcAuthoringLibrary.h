#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWNpcAuthoringLibrary.generated.h"

class UAnimBlueprint;
class UBlendSpace;
class USkeleton;
class USkeletalMesh;

/** Explicit authoring for a simple NPC graph; never runs on gameplay tick. */
UCLASS()
class CLASSFEATUREEDITOR_API USWNpcAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Connect the one existing BlendSpace player to the one root and compile.
     * Refuses graphs with a different shape instead of replacing authored logic.
     */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|NPC")
    static bool ConfigureNpcLocomotionGraph(UAnimBlueprint* Blueprint, USkeleton* Skeleton, UBlendSpace* BlendSpace);

    /** Repair bindings only: preserve the authored graph, animations and sample speeds. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|NPC")
    static bool RepairNpcAnimationBindings(UAnimBlueprint* Blueprint, USkeletalMesh* Mesh, UBlendSpace* BlendSpace);
};
