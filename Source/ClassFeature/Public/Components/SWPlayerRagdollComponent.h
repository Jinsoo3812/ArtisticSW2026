#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Components/SkinnedMeshComponent.h"
#include "SWPlayerRagdollComponent.generated.h"

/** Presentation only. Health replication and GameMode retain life/respawn ownership. */
UCLASS(ClassGroup=(Player), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API USWPlayerRagdollComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USWPlayerRagdollComponent();
    bool StartRagdoll();
    void RestoreLivingPresentation();
    bool IsRagdollActive() const { return bActive; }
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Brief visual transition; physics itself starts from the evaluated death pose immediately. */
    UPROPERTY(EditDefaultsOnly, Category="Death|Presentation", meta=(ClampMin="0",ClampMax="0.5"))
    float BlendDuration = 0.12f;
    UPROPERTY(EditDefaultsOnly, Category="Death|Momentum", meta=(ClampMin="0"))
    float MaximumInheritedSpeed = 1800.f;
    /** A bounded, mass-aware impulse at the reported physical ancestor, never a fixed facing kick. */
    UPROPERTY(EditDefaultsOnly, Category="Death|Momentum", meta=(ClampMin="0",ClampMax="500"))
    float HitVelocityChange = 120.f;

private:
    struct FAttachedPresentation
    {
        TWeakObjectPtr<AActor> Actor;
        bool bHidden = false;
        bool bCollision = false;
    };
    TArray<FAttachedPresentation> SavedAttachments;
    bool bActive = false;
    bool bSaved = false;
    bool bReportedMissingAsset = false;
    float BlendElapsed = 0;
    FTransform SavedMeshRelativeTransform;
    FName SavedCollisionProfile;
    FCollisionResponseContainer SavedResponses;
    TEnumAsByte<ECollisionEnabled::Type> SavedMeshCollision;
    TEnumAsByte<ECollisionEnabled::Type> SavedCapsuleCollision;
    TWeakObjectPtr<USceneComponent> SavedMeshParent;
    FName SavedMeshSocket;
    bool bSavedActorCollision = true;
    bool bSavedActorHidden = false;
    bool bSavedMeshVisible = true;
    bool bSavedMeshHidden = false;
    bool bSavedPauseAnims = false;
    bool bSavedUpdateRateOptimizations = false;
    bool bSavedGravity = true;
    bool bSavedOverlapEvents = false;
    bool bSavedBlendPhysics = false;
    bool bSavedServerPhysics = false;
    TMap<FName,bool> SavedBodyCCD;
    EVisibilityBasedAnimTickOption SavedTickOption;
};
