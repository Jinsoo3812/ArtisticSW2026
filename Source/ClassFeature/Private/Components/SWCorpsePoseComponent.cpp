#include "Components/SWCorpsePoseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Modules/ModuleManager.h"

USWCorpsePoseComponent::USWCorpsePoseComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
}

void USWCorpsePoseComponent::Initialize(USkeletalMeshComponent* Mesh)
{
    Source=Mesh;if(!Source||!Source->GetSkeletalMeshAsset())return;
    RefCS=Source->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
    const auto& Ref=Source->GetSkeletalMeshAsset()->GetRefSkeleton();
    for(int32 I=0;I<RefCS.Num();++I){const int32 P=Ref.GetParentIndex(I);if(P!=INDEX_NONE)RefCS[I]=RefCS[I]*RefCS[P];}
    Visual=NewObject<UPoseableMeshComponent>(GetOwner(),TEXT("CorpseCorrectedVisual"));
    GetOwner()->AddInstanceComponent(Visual);
    Visual->SetupAttachment(Source);
    Visual->SetSkinnedAssetAndUpdate(Source->GetSkeletalMeshAsset());
    for(int32 I=0;I<Source->GetNumMaterials();++I)Visual->SetMaterial(I,Source->GetMaterial(I));
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetGenerateOverlapEvents(false);
    Visual->SetCanEverAffectNavigation(false);Visual->SetVisibility(false);Visual->bUseAttachParentBound=true;
    Visual->RegisterComponent();Visual->SetComponentTickEnabled(false);
    AddTickPrerequisiteComponent(Source);
    SetComponentTickEnabled(true);
    bOriginalMainPass=Source->bRenderInMainPass;bOriginalDepthPass=Source->bRenderInDepthPass;bOriginalShadow=Source->CastShadow;
}

void USWCorpsePoseComponent::RestoreSource()
{
    if(!bWasActive)return;
    if(Source){Source->SetRenderInMainPass(bOriginalMainPass);Source->SetRenderInDepthPass(bOriginalDepthPass);Source->SetCastShadow(bOriginalShadow);}
    if(Visual)Visual->SetVisibility(false);bWasActive=false;
}

void USWCorpsePoseComponent::RestoreLivingPresentation(){RestoreSource();SetComponentTickEnabled(false);}

void USWCorpsePoseComponent::TickComponent(float DeltaTime,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,Tick,Function);
    if(Source&&Visual)
        for(int32 I=0;I<Source->GetNumMaterials();++I)
            if(Visual->GetMaterial(I)!=Source->GetMaterial(I))Visual->SetMaterial(I,Source->GetMaterial(I));
    if(!Source||!Visual||!Source->IsAnySimulatingPhysics()){RestoreSource();return;}
    const auto& Ref=Source->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform> Original=Source->GetComponentSpaceTransforms();
    if(Original.Num()!=RefCS.Num()||Visual->BoneSpaceTransforms.Num()!=Original.Num())return;
    TArray<FTransform> Pose=Original;
    TArray<bool> Changed;Changed.Init(false,Pose.Num());CorrectedBones=0;MaximumHelperPositionChange=0;
    auto Index=[&](const FString& Name){return Ref.FindBoneIndex(FName(*Name));};
    auto FixTwist=[&](const FString& Stem,const FString& End,const FString& Side)
    {
        const int32 A=Index(Stem+TEXT("_")+Side),B=Index(End+TEXT("_")+Side);if(A==INDEX_NONE||B==INDEX_NONE)return;
        const FQuat AlignedEnd=Original[B].GetRotation()*RefCS[B].GetRotation().Inverse()*RefCS[A].GetRotation();
        FQuat Swing,Twist;(Original[A].GetRotation().Inverse()*AlignedEnd).GetNormalized().ToSwingTwist(FVector::ForwardVector,Swing,Twist);
        if(Twist.W<0)Twist=Twist*-1.;
        for(int32 N=1;N<=2;++N)
        {
            const int32 I=Index(Stem+FString::Printf(TEXT("_twist_%02d_"),N)+Side);if(I==INDEX_NONE)continue;
            const FVector RefSegment=RefCS[B].GetLocation()-RefCS[A].GetLocation();
            const double Alpha=FMath::Clamp(FVector::DotProduct(RefCS[I].GetLocation()-RefCS[A].GetLocation(),RefSegment)/FMath::Max(RefSegment.SizeSquared(),1.e-8),0.,1.);
            Pose[I].SetLocation(FMath::Lerp(Original[A].GetLocation(),Original[B].GetLocation(),Alpha));
            const FQuat Offset=RefCS[A].GetRotation().Inverse()*RefCS[I].GetRotation();
            Pose[I].SetRotation((Original[A].GetRotation()*FQuat::Slerp(FQuat::Identity,Twist,Alpha)*Offset).GetNormalized());
            Changed[I]=true;
        }
    };
    for(const FString Side:{TEXT("l"),TEXT("r")})
    { FixTwist(TEXT("upperarm"),TEXT("lowerarm"),Side);FixTwist(TEXT("lowerarm"),TEXT("hand"),Side);FixTwist(TEXT("thigh"),TEXT("calf"),Side);FixTwist(TEXT("calf"),TEXT("foot"),Side); }
    auto FixGap=[&](const TCHAR* Name,const TCHAR* Start,const TCHAR* End)
    {
        const int32 I=Index(Name),A=Index(Start),B=Index(End);if(I==INDEX_NONE||A==INDEX_NONE||B==INDEX_NONE)return;
        const FVector Segment=RefCS[B].GetLocation()-RefCS[A].GetLocation();
        const double Alpha=FMath::Clamp(FVector::DotProduct(RefCS[I].GetLocation()-RefCS[A].GetLocation(),Segment)/FMath::Max(Segment.SizeSquared(),1.e-8),0.,1.);
        const FQuat DA=Original[A].GetRotation()*RefCS[A].GetRotation().Inverse(),DB=Original[B].GetRotation()*RefCS[B].GetRotation().Inverse();
        const FVector PA=Original[A].GetLocation()+DA.RotateVector(RefCS[I].GetLocation()-RefCS[A].GetLocation());
        const FVector PB=Original[B].GetLocation()+DB.RotateVector(RefCS[I].GetLocation()-RefCS[B].GetLocation());
        Pose[I].SetLocation(FMath::Lerp(PA,PB,Alpha));Pose[I].SetRotation((FQuat::Slerp(DA,DB,Alpha)*RefCS[I].GetRotation()).GetNormalized());Changed[I]=true;
    };
    FixGap(TEXT("spine_01"),TEXT("pelvis"),TEXT("spine_02"));FixGap(TEXT("spine_03"),TEXT("spine_02"),TEXT("spine_04"));
    FixGap(TEXT("spine_05"),TEXT("spine_04"),TEXT("neck_01"));FixGap(TEXT("neck_02"),TEXT("neck_01"),TEXT("head"));
    FixGap(TEXT("clavicle_l"),TEXT("spine_04"),TEXT("upperarm_l"));FixGap(TEXT("clavicle_r"),TEXT("spine_04"),TEXT("upperarm_r"));
    const UPhysicsAsset* PA=Source->GetPhysicsAsset();
    for(int32 I=0;I<Pose.Num();++I)
    {
        const int32 P=Ref.GetParentIndex(I);const bool bPhysical=PA&&PA->FindBodyIndex(Ref.GetBoneName(I))!=INDEX_NONE;
        if(!Changed[I]&&P!=INDEX_NONE&&Changed[P]&&!bPhysical){Pose[I]=Original[I].GetRelativeTransform(Original[P])*Pose[P];Changed[I]=true;}
        if(Changed[I]){++CorrectedBones;MaximumHelperPositionChange=FMath::Max(MaximumHelperPositionChange,FVector::Dist(Original[I].GetLocation(),Pose[I].GetLocation()));}
        Visual->BoneSpaceTransforms[I]=P==INDEX_NONE?Pose[I]:Pose[I].GetRelativeTransform(Pose[P]);
    }
    Visual->SetWorldTransform(Source->GetComponentTransform());Visual->RefreshBoneTransforms();
    if(!bWasActive){Source->SetRenderInMainPass(false);Source->SetRenderInDepthPass(false);Source->SetCastShadow(false);Visual->SetVisibility(true);bWasActive=true;}
}

void USWCorpsePoseComponent::EndPlay(const EEndPlayReason::Type Reason){RestoreSource();Super::EndPlay(Reason);}
USWCorpsePoseComponent* USWCorpsePoseLibrary::Install(ACharacter* Character)
{
    if(!Character||!Character->GetMesh()||Character->GetNetMode()==NM_DedicatedServer)return nullptr;
    if(auto* Existing=Character->FindComponentByClass<USWCorpsePoseComponent>()){Existing->SetComponentTickEnabled(true);return Existing;}
    auto* Component=NewObject<USWCorpsePoseComponent>(Character);Character->AddInstanceComponent(Component);Component->RegisterComponent();Component->Initialize(Character->GetMesh());return Component;
}
UPoseableMeshComponent* USWCorpsePoseLibrary::GetVisual(ACharacter* Character)
{
    const auto* C=Character?Character->FindComponentByClass<USWCorpsePoseComponent>():nullptr;return C?C->Visual:nullptr;
}
