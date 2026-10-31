#include "Components/SWPlayerRagdollComponent.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Engine/SkeletalMesh.h"

USWPlayerRagdollComponent::USWPlayerRagdollComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool USWPlayerRagdollComponent::StartRagdoll()
{
    if (bActive) return true;
    ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
    USkeletalMeshComponent* Mesh = Player ? Player->GetMesh() : nullptr;
    if (!Mesh) return false;
    if (!bSaved)
    {
        SavedMeshRelativeTransform = Mesh->GetRelativeTransform();
        SavedMeshParent = Mesh->GetAttachParent(); SavedMeshSocket = Mesh->GetAttachSocketName();
        SavedCollisionProfile = Mesh->GetCollisionProfileName(); SavedResponses = Mesh->GetCollisionResponseToChannels();
        SavedMeshCollision = Mesh->GetCollisionEnabled();
        SavedCapsuleCollision = Player->GetCapsuleComponent()->GetCollisionEnabled();
        bSavedActorCollision = Player->GetActorEnableCollision(); bSavedActorHidden = Player->IsHidden();
        bSavedMeshVisible = Mesh->IsVisible(); bSavedMeshHidden = Mesh->bHiddenInGame;
        bSavedPauseAnims = Mesh->bPauseAnims; bSavedUpdateRateOptimizations = Mesh->bEnableUpdateRateOptimizations;
        SavedTickOption = Mesh->VisibilityBasedAnimTickOption;
        bSavedGravity=Mesh->IsGravityEnabled(); bSavedOverlapEvents=Mesh->GetGenerateOverlapEvents();
        bSavedBlendPhysics=Mesh->bBlendPhysics; bSavedServerPhysics=Mesh->bEnablePhysicsOnDedicatedServer;
        for(const FBodyInstance* Body:Mesh->Bodies) if(Body)
            if(const auto* Setup=Cast<USkeletalBodySetup>(Body->GetBodySetup())) SavedBodyCCD.Add(Setup->BoneName,Body->bUseCCD);
        TArray<AActor*> Attached; Player->GetAttachedActors(Attached,true,true);
        for (AActor* Actor:Attached) if (IsValid(Actor)) SavedAttachments.Add({Actor,Actor->IsHidden(),Actor->GetActorEnableCollision()});
        bSaved = true;
    }
    if (!Mesh->GetPhysicsAsset() || Mesh->GetPhysicsAsset()->FindBodyIndex(TEXT("pelvis")) == INDEX_NONE)
    {
        if (!bReportedMissingAsset) UE_LOG(LogTemp, Warning, TEXT("Player corpse %s has no valid pelvis PhysicsAsset; keeping the safe hidden presentation"), *Player->GetName());
        bReportedMissingAsset = true;
        return false;
    }

    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    FVector Velocity = Player->GetVelocity();
    if (Movement && Movement->IsMovingOnGround())
    {
        // Ship movement is base transport, not part of CharacterMovement::Velocity.
        const UPrimitiveComponent* Base = Player->GetMovementBase();
        Velocity += MovementBaseUtility::GetMovementBaseVelocity(Base, Player->GetBasedMovement().BoneName);
        Velocity += MovementBaseUtility::GetMovementBaseTangentialVelocity(Base, Player->GetBasedMovement().BoneName, Mesh->GetSocketLocation(TEXT("pelvis")));
    }
    if (Velocity.ContainsNaN()) Velocity = FVector::ZeroVector;
    Velocity = Velocity.GetClampedToMaxSize(FMath::Max(0.f, MaximumInheritedSpeed));
    Player->PrepareDeathMovement();
    Player->SetActorEnableCollision(true);
    Player->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Player->SetActorHiddenInGame(false); Mesh->SetVisibility(true); Mesh->SetHiddenInGame(false);
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->bEnableUpdateRateOptimizations = false;
    // Freeze the final animated pose; simulated bodies are the only subsequent pose writer.
    Mesh->bPauseAnims = true;
    Mesh->bEnablePhysicsOnDedicatedServer = true;
    Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
    Mesh->SetCollisionObjectType(ECC_PhysicsBody);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetGenerateOverlapEvents(false); Mesh->SetEnableGravity(true); Mesh->SetAllUseCCD(true);
    Mesh->SetAllBodiesSimulatePhysics(true); Mesh->SetSimulatePhysics(true);
    // SkeletalMeshComponent replaces PA iteration counts with its component defaults
    // during body creation. Reapply only authored overrides, once at this transition.
    for (FBodyInstance* Body:Mesh->Bodies) if (Body)
    {
        if (const auto* Setup=Cast<USkeletalBodySetup>(Body->GetBodySetup()))
        {
            const FBodyInstance& Authored=Setup->DefaultInstance;
            const int32 Positions=Authored.GetPositionSolverIterationCount();
            Body->SetOverrideIterationCounts(Positions>=0);
            if (Positions>=0)
            {
                Body->SetPositionSolverIterationCount(Positions);
                Body->SetVelocitySolverIterationCount(Authored.GetVelocitySolverIterationCount());
                Body->SetProjectionSolverIterationCount(Authored.GetProjectionSolverIterationCount());
            }
        }
    }
    Mesh->SetAllPhysicsLinearVelocity(Velocity); Mesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);

    if (const UBaseHealthComponent* Health = Player->GetHealthComponent())
    {
        const FDeathRagdollImpactData Hit = Health->GetDeathRagdollImpactData();
        const FVector Direction = FVector(Hit.KnockbackDirection).GetSafeNormal();
        if (Hit.bHasDirection && !Direction.ContainsNaN() && HitVelocityChange > 0)
        {
            FName Bone = Hit.HitBoneName;
            for (int32 Index=Mesh->GetBoneIndex(Bone); Index!=INDEX_NONE && !Mesh->GetBodyInstance(Bone); )
            { Index=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(Index); Bone=Index==INDEX_NONE ? NAME_None : Mesh->GetBoneName(Index); }
            if (Bone.IsNone() || !Mesh->GetBodyInstance(Bone)) Bone=TEXT("pelvis");
            if (FBodyInstance* Body = Mesh->GetBodyInstance(Bone))
            {
                const FVector Center = Body->GetCOMPosition();
                const FVector Point = Hit.bHasImpactPoint && !FVector(Hit.ImpactPoint).ContainsNaN()
                    ? Center+(FVector(Hit.ImpactPoint)-Center).GetClampedToMaxSize(20.f) : Center;
                Mesh->AddImpulseAtLocation(Direction*FMath::Clamp(HitVelocityChange,0.f,500.f)*Body->GetBodyMass(), Point, Bone);
            }
        }
    }
    Mesh->WakeAllRigidBodies();
    bActive=true; BlendElapsed=0;
    Mesh->bBlendPhysics = BlendDuration<=0;
    Mesh->SetAllBodiesPhysicsBlendWeight(BlendDuration>0 ? 0.01f : 1.f);
    SetComponentTickEnabled(BlendDuration>0);
    return true;
}

void USWPlayerRagdollComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    ACharacter* Player=Cast<ACharacter>(GetOwner());
    if (!bActive || !Player || !Player->GetMesh()) { SetComponentTickEnabled(false); return; }
    BlendElapsed+=DeltaTime;
    const float Alpha=FMath::Clamp(BlendElapsed/FMath::Max(BlendDuration,SMALL_NUMBER),0.f,1.f);
    Player->GetMesh()->SetAllBodiesPhysicsBlendWeight(FMath::SmoothStep(0.f,1.f,Alpha));
    if (Alpha>=1) { Player->GetMesh()->bBlendPhysics=true; SetComponentTickEnabled(false); }
}

void USWPlayerRagdollComponent::RestoreLivingPresentation()
{
    SetComponentTickEnabled(false); bActive=false;
    ACharacter* Player=Cast<ACharacter>(GetOwner()); USkeletalMeshComponent* Mesh=Player?Player->GetMesh():nullptr;
    if (!bSaved || !Mesh) return;
    Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector); Mesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Mesh->SetSimulatePhysics(false); Mesh->SetAllBodiesSimulatePhysics(false); Mesh->SetAllBodiesPhysicsBlendWeight(0);
    if (SavedMeshParent.IsValid() && Mesh->GetAttachParent()!=SavedMeshParent.Get()) Mesh->AttachToComponent(SavedMeshParent.Get(),FAttachmentTransformRules::KeepWorldTransform,SavedMeshSocket);
    Mesh->SetRelativeTransform(SavedMeshRelativeTransform,false,nullptr,ETeleportType::TeleportPhysics);
    Mesh->SetCollisionProfileName(SavedCollisionProfile); Mesh->SetCollisionResponseToChannels(SavedResponses); Mesh->SetCollisionEnabled(SavedMeshCollision);
    Mesh->SetEnableGravity(bSavedGravity); Mesh->SetGenerateOverlapEvents(bSavedOverlapEvents);
    Mesh->bBlendPhysics=bSavedBlendPhysics; Mesh->bEnablePhysicsOnDedicatedServer=bSavedServerPhysics;
    for(const auto& Entry:SavedBodyCCD) if(FBodyInstance* Body=Mesh->GetBodyInstance(Entry.Key)) Body->SetUseCCD(Entry.Value);
    SavedBodyCCD.Reset();
    Mesh->bPauseAnims=bSavedPauseAnims; Mesh->bEnableUpdateRateOptimizations=bSavedUpdateRateOptimizations; Mesh->VisibilityBasedAnimTickOption=SavedTickOption;
    Mesh->SetVisibility(bSavedMeshVisible); Mesh->SetHiddenInGame(bSavedMeshHidden);
    Player->SetActorEnableCollision(bSavedActorCollision); Player->SetActorHiddenInGame(bSavedActorHidden); Player->GetCapsuleComponent()->SetCollisionEnabled(SavedCapsuleCollision);
    for (const FAttachedPresentation& Entry:SavedAttachments)
        if (AActor* Actor=Entry.Actor.Get(); Actor && Actor->IsAttachedTo(Player))
        { Actor->SetActorHiddenInGame(Entry.bHidden); Actor->SetActorEnableCollision(Entry.bCollision); }
    SavedAttachments.Reset();
    bSaved=false; bReportedMissingAsset=false;
}
