#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWCorpsePoseComponent.generated.h"
class ACharacter;
class USkeletalMeshComponent;
class UPoseableMeshComponent;

/** A post-physics visual pose; never writes to simulated body transforms. */
UCLASS()
class CLASSFEATURE_API USWCorpsePoseComponent:public UActorComponent
{
    GENERATED_BODY()
public:
    USWCorpsePoseComponent();
    void Initialize(USkeletalMeshComponent* Mesh);
    void RestoreLivingPresentation();
    virtual void TickComponent(float DeltaTime,ELevelTick Tick,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Source;
    UPROPERTY() TObjectPtr<UPoseableMeshComponent> Visual;
    int32 CorrectedBones=0;
    double MaximumHelperPositionChange=0;
private:
    TArray<FTransform> RefCS;
    bool bWasActive=false;
    bool bOriginalMainPass=true,bOriginalDepthPass=true,bOriginalShadow=true;
    void RestoreSource();
};

UCLASS()
class CLASSFEATURE_API USWCorpsePoseLibrary:public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Corpse Pose")
    static USWCorpsePoseComponent* Install(ACharacter* Character);
    UFUNCTION(BlueprintCallable,Category="Corpse Pose")
    static UPoseableMeshComponent* GetVisual(ACharacter* Character);
};
