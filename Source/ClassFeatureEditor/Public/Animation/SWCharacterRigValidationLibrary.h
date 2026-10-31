#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWCharacterRigValidationLibrary.generated.h"
class UAnimBlueprint;
class UBlueprint;
class USkeletalMesh;
class ACharacter;
class UPrimitiveComponent;

/** Editor-only inspection and explicit authoring for character rig migration. */
UCLASS()
class CLASSFEATUREEDITOR_API USWCharacterRigValidationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static FString DescribeGraph(UAnimBlueprint* Blueprint);
    /** Only named ModifyBone nodes are accepted; validate the entire set before mutation. */
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool BypassFixedBoneCorrections(UAnimBlueprint* Blueprint, const TArray<FName>& NodeNames);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static FString SnapshotCharacter(ACharacter* Character);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static FString DescribeMesh(USkeletalMesh* Mesh);
    /** New PA is fitted from this mesh, never reused from a different bind. */
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool EnsureRigPhysics(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool ConfigureCharacter(UBlueprint* Blueprint, USkeletalMesh* Mesh, UAnimBlueprint* Animation);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static ACharacter* SpawnTestCharacter(UWorld* World, UBlueprint* Blueprint, FVector Location);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool ConfigureTestGameMode(UBlueprint* Blueprint);
    /** Rebuild only final limb markers on this mesh; preserve authored gun/root. */
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool ConfigureFinalLimbMarkers(UAnimBlueprint* Blueprint, USkeletalMesh* Mesh);
    /** Explicit offline PA authoring, scoped to the two migrated player meshes. */
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool ConfigureRagdollPhysics(USkeletalMesh* Mesh, const FString& DescriptorJson);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static FString DescribeRagdollPhysics(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static FString SnapshotRagdoll(ACharacter* Character);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool ConfigureRagdollTestGameMode(UBlueprint* Blueprint);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static bool KillRagdollTestCharacter(ACharacter* Character, bool bDirectionalHit = false);
    /** Exercise production progress restore with its mandatory life-initialization barrier. */
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static ACharacter* SpawnRagdollTestNewLife(APlayerController* Controller, UBlueprint* Blueprint, FVector Location);
    UFUNCTION(BlueprintCallable, Category="Animation|Rig Validation")
    static void SetRagdollTestMovementBase(ACharacter* Character, UPrimitiveComponent* Base, FVector Velocity);
};
