#include "Animation/SWCharacterRigValidationLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "BaseAttributeSet.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameplayEffect.h"
#include "Room/SWRoomSaveGame.h"

namespace
{
using FJson=TSharedPtr<FJsonObject>;
FVector ReadVector(const FJson& O,const TCHAR* Key)
{
    const auto& A=O->GetArrayField(Key);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
}
FQuat ReadRotation(const FJson& O,const TCHAR* Key)
{
    const auto& A=O->GetArrayField(Key);return FQuat(A[1]->AsNumber(),A[2]->AsNumber(),A[3]->AsNumber(),A[0]->AsNumber()).GetNormalized();
}
TArray<FTransform> RefTransforms(USkeletalMesh* Mesh)
{
    const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Result=Ref.GetRefBonePose();
    for(int32 I=0;I<Result.Num();++I){const int32 P=Ref.GetParentIndex(I);if(P!=INDEX_NONE)Result[I]=Result[I]*Result[P];}
    return Result;
}
TSharedPtr<FJsonValue> V3(const FVector& V)
{
    return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
}
FString Stringify(const FJson& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
}

bool USWCharacterRigValidationLibrary::ConfigureRagdollPhysics(USkeletalMesh* Mesh,const FString& DescriptorJson)
{
    if(!Mesh||(!Mesh->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChManUE5/"))&&!Mesh->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChWomanUE5/"))))return false;
    FJson Doc;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DescriptorJson),Doc)||!Doc)return false;
    if(Doc->GetStringField(TEXT("mesh"))!=Mesh->GetPackage()->GetName())return false;
    const auto& Bodies=Doc->GetArrayField(TEXT("bodies"));const auto& Joints=Doc->GetArrayField(TEXT("joints"));
    const FJson Settings=Doc->GetObjectField(TEXT("world_solver"));
    if(Bodies.Num()!=17||Joints.Num()!=16)return false;
    const auto& Ref=Mesh->GetRefSkeleton();TSet<FName> Names,Children;
    for(const auto& Value:Bodies)
    {
        auto B=Value->AsObject();FName Name(*B->GetStringField(TEXT("bone")));
        if(Names.Contains(Name)||Ref.FindBoneIndex(Name)==INDEX_NONE||B->GetNumberField(TEXT("mass_kg"))<=0||ReadVector(B,TEXT("center")).ContainsNaN())return false;
        if(B->GetStringField(TEXT("shape"))==TEXT("box")){if(ReadVector(B,TEXT("dimensions")).GetMin()<=0)return false;}
        else if(B->GetNumberField(TEXT("radius"))<=0||B->GetNumberField(TEXT("length"))<=0)return false;
        Names.Add(Name);
    }
    if(!Names.Contains(TEXT("pelvis")))return false;
    for(const auto& Value:Joints)
    {
        auto J=Value->AsObject();FName Child(*J->GetStringField(TEXT("child"))),Parent(*J->GetStringField(TEXT("parent")));
        if(!Names.Contains(Child)||!Names.Contains(Parent)||Children.Contains(Child)||Child==TEXT("pelvis")||Child==Parent)return false;
        Children.Add(Child);
    }
    if(!EnsureRigPhysics(Mesh))return false;
    auto* PA=Mesh->GetPhysicsAsset();PA->Modify();Mesh->Modify();
    const FString PMPath=TEXT("/Game/jiwon/Characters/Physics/PM_PlayerCorpse");
    auto* Material=LoadObject<UPhysicalMaterial>(nullptr,*(PMPath+TEXT(".PM_PlayerCorpse")));
    if(!Material)
    {
        Material=NewObject<UPhysicalMaterial>(CreatePackage(*PMPath),TEXT("PM_PlayerCorpse"),RF_Public|RF_Standalone|RF_Transactional);
        FAssetRegistryModule::AssetCreated(Material);
    }
    Material->Modify();Material->Friction=.7f;Material->StaticFriction=.8f;Material->Restitution=.02f;Material->MarkPackageDirty();
    PA->SkeletalBodySetups.Reset();PA->ConstraintSetup.Reset();PA->CollisionDisableTable.Reset();
    const auto CS=RefTransforms(Mesh);
    for(const auto& Value:Bodies)
    {
        auto B=Value->AsObject();FName Bone(*B->GetStringField(TEXT("bone")));const int32 Index=Ref.FindBoneIndex(Bone);
        auto* Setup=NewObject<USkeletalBodySetup>(PA,MakeUniqueObjectName(PA,USkeletalBodySetup::StaticClass(),FName(*(TEXT("Body_")+Bone.ToString()))),RF_Transactional);
        Setup->BoneName=Bone;Setup->PhysicsType=PhysType_Default;Setup->PhysMaterial=Material;Setup->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        FBodyInstance& BI=Setup->DefaultInstance;BI.SetOverrideIterationCounts(true);BI.SetPositionSolverIterationCount(FMath::Clamp(Settings->GetIntegerField(TEXT("position_iterations")),1,64));BI.SetVelocitySolverIterationCount(FMath::Clamp(Settings->GetIntegerField(TEXT("velocity_iterations")),1,32));BI.SetProjectionSolverIterationCount(1);
        BI.SetMassOverride(B->GetNumberField(TEXT("mass_kg")),true);BI.LinearDamping=Settings->GetNumberField(TEXT("linear_damping"));BI.AngularDamping=Settings->GetNumberField(TEXT("angular_damping"));BI.InertiaTensorScale=FVector(1.2f);
        BI.bUseCCD=true;BI.SetMaxAngularVelocityInRadians(12.f,false);BI.SetMaxDepenetrationVelocity(120.f);
        const FTransform World(ReadRotation(B,TEXT("rotation")),ReadVector(B,TEXT("center")));
        const FTransform Local=World.GetRelativeTransform(CS[Index]);
        if(B->GetStringField(TEXT("shape"))==TEXT("box"))
        {
            FKBoxElem Shape;Shape.Center=Local.GetLocation();Shape.Rotation=Local.Rotator();const FVector Size=ReadVector(B,TEXT("dimensions"));Shape.X=Size.X;Shape.Y=Size.Y;Shape.Z=Size.Z;Setup->AggGeom.BoxElems.Add(Shape);
        }
        else
        {
            FKSphylElem Shape;Shape.Center=Local.GetLocation();Shape.Rotation=Local.Rotator();Shape.Radius=B->GetNumberField(TEXT("radius"));Shape.Length=B->GetNumberField(TEXT("length"));Setup->AggGeom.SphylElems.Add(Shape);
        }
        Setup->InvalidatePhysicsData();Setup->CreatePhysicsMeshes();PA->SkeletalBodySetups.Add(Setup);
    }
    PA->UpdateBodySetupIndexMap();
    for(const auto& Value:Joints)
    {
        auto J=Value->AsObject();FName Child(*J->GetStringField(TEXT("child"))),Parent(*J->GetStringField(TEXT("parent")));
        auto* Template=NewObject<UPhysicsConstraintTemplate>(PA,MakeUniqueObjectName(PA,UPhysicsConstraintTemplate::StaticClass(),FName(*(TEXT("Joint_")+Child.ToString()))),RF_Transactional);
        FConstraintInstance& C=Template->DefaultInstance;C.JointName=Child;C.ConstraintBone1=Child;C.ConstraintBone2=Parent;
        const FVector Pivot=ReadVector(J,TEXT("pivot")),Bias=ReadVector(J,TEXT("bias_xyz_deg"));
        const FQuat Q=ReadRotation(J,TEXT("rotation"));
        const FQuat Offset=FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Bias.X))*FQuat(FVector::RightVector,FMath::DegreesToRadians(Bias.Y))*FQuat(FVector::UpVector,FMath::DegreesToRadians(Bias.Z));
        C.SetRefFrame(EConstraintFrame::Frame1,FTransform(Q,Pivot).GetRelativeTransform(CS[Ref.FindBoneIndex(Child)]));
        C.SetRefFrame(EConstraintFrame::Frame2,FTransform(Q*Offset,Pivot).GetRelativeTransform(CS[Ref.FindBoneIndex(Parent)]));
        C.SetLinearXMotion(LCM_Locked);C.SetLinearYMotion(LCM_Locked);C.SetLinearZMotion(LCM_Locked);
        const FVector Limit=ReadVector(J,TEXT("swing1_swing2_twist_deg"));
        C.SetAngularSwing1Limit(Limit.X>0?ACM_Limited:ACM_Locked,Limit.X);C.SetAngularSwing2Limit(Limit.Y>0?ACM_Limited:ACM_Locked,Limit.Y);C.SetAngularTwistLimit(Limit.Z>0?ACM_Limited:ACM_Locked,Limit.Z);
        C.ProfileInstance.ConeLimit.bSoftConstraint=false;C.ProfileInstance.TwistLimit.bSoftConstraint=false;C.ProfileInstance.bAngularBreakable=false;C.ProfileInstance.bLinearBreakable=false;
        C.ProfileInstance.bEnableMassConditioning=true;C.ProfileInstance.bUseLinearJointSolver=false;
        C.SetParentDominates(false);C.SetShockPropagationParams(false,0.f);C.SetProjectionParams(true,Settings->GetNumberField(TEXT("projection_linear_alpha")),Settings->GetNumberField(TEXT("projection_angular_alpha")),Settings->GetNumberField(TEXT("projection_linear_tolerance_cm")),Settings->GetNumberField(TEXT("projection_angular_tolerance_deg")));C.SetDisableCollision(true);
        // Editor serialization saves DefaultProfile, not the currently displayed instance.
        Template->SetDefaultProfile(C);
        PA->ConstraintSetup.Add(Template);PA->DisableCollision(PA->FindBodyIndex(Child),PA->FindBodyIndex(Parent));
    }
    PA->UpdateBoundsBodiesArray();PA->SetPreviewMesh(Mesh);PA->MarkPackageDirty();Mesh->MarkPackageDirty();
    return true;
}

FString USWCharacterRigValidationLibrary::DescribeRagdollPhysics(USkeletalMesh* Mesh)
{
    auto O=MakeShared<FJsonObject>();auto* PA=Mesh?Mesh->GetPhysicsAsset():nullptr;if(!PA)return Stringify(O);
    O->SetStringField(TEXT("asset"),PA->GetPathName());O->SetNumberField(TEXT("disabled_pairs"),PA->CollisionDisableTable.Num());
    TArray<TSharedPtr<FJsonValue>> Bodies,Joints;
    for(const USkeletalBodySetup* B:PA->SkeletalBodySetups)if(B)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("bone"),B->BoneName.ToString());Row->SetNumberField(TEXT("shapes"),B->AggGeom.GetElementCount());
        Row->SetNumberField(TEXT("mass_kg"),B->DefaultInstance.GetMassOverride());Row->SetNumberField(TEXT("position_iterations"),B->DefaultInstance.GetPositionSolverIterationCount());Row->SetNumberField(TEXT("velocity_iterations"),B->DefaultInstance.GetVelocitySolverIterationCount());Bodies.Add(MakeShared<FJsonValueObject>(Row));
    }
    for(const UPhysicsConstraintTemplate* T:PA->ConstraintSetup)if(T)
    {
        const auto& C=T->DefaultInstance;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("child"),C.ConstraintBone1.ToString());Row->SetStringField(TEXT("parent"),C.ConstraintBone2.ToString());
        Row->SetNumberField(TEXT("swing1"),C.GetAngularSwing1Limit());Row->SetNumberField(TEXT("swing2"),C.GetAngularSwing2Limit());Row->SetNumberField(TEXT("twist"),C.GetAngularTwistLimit());Row->SetBoolField(TEXT("collision_disabled"),C.IsCollisionDisabled());Joints.Add(MakeShared<FJsonValueObject>(Row));
        Row->SetNumberField(TEXT("swing1_motion"),C.GetAngularSwing1Motion());Row->SetNumberField(TEXT("swing2_motion"),C.GetAngularSwing2Motion());Row->SetNumberField(TEXT("twist_motion"),C.GetAngularTwistMotion());
    }
    O->SetArrayField(TEXT("bodies"),Bodies);O->SetArrayField(TEXT("joints"),Joints);return Stringify(O);
}

FString USWCharacterRigValidationLibrary::SnapshotRagdoll(ACharacter* Character)
{
    auto O=MakeShared<FJsonObject>();USkeletalMeshComponent* Mesh=Character?Character->GetMesh():nullptr;if(!Mesh)return Stringify(O);
    O->SetStringField(TEXT("actor"),Character->GetName());O->SetBoolField(TEXT("authority"),Character->HasAuthority());O->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh->GetSkeletalMeshAsset()));
    if(const auto* Player=Cast<ABasePlayer>(Character))
    {
        const auto* Health=Player->GetHealthComponent();O->SetBoolField(TEXT("health_dead"),Health&&Health->IsDead());O->SetNumberField(TEXT("health_state"),Health?static_cast<int32>(Health->GetDeathState()):-1);
        const auto* ASC=Player->GetAbilitySystemComponent();O->SetStringField(TEXT("asc_avatar"),ASC?GetNameSafe(ASC->GetAvatarActor()):TEXT("None"));
    }
    O->SetBoolField(TEXT("simulating"),Mesh->IsAnySimulatingPhysics());O->SetBoolField(TEXT("visible"),!Character->IsHidden()&&Mesh->IsVisible());
    O->SetNumberField(TEXT("time"),Character->GetWorld()->GetTimeSeconds());O->SetNumberField(TEXT("movement_mode"),Character->GetCharacterMovement()->MovementMode);
    TArray<TSharedPtr<FJsonValue>> Bodies,Joints;
    for(const FBodyInstance* B:Mesh->Bodies)if(B)
    {
        const auto* Setup=Cast<USkeletalBodySetup>(B->GetBodySetup());
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("bone"),Setup?Setup->BoneName.ToString():TEXT("None"));const auto T=B->GetUnrealWorldTransform();
        if(Setup)Row->SetField(TEXT("render_bone_position"),V3(Mesh->GetSocketLocation(Setup->BoneName)));
        Row->SetNumberField(TEXT("position_iterations"),B->GetPositionSolverIterationCount());Row->SetNumberField(TEXT("velocity_iterations"),B->GetVelocitySolverIterationCount());
        Row->SetField(TEXT("position"),V3(T.GetLocation()));Row->SetField(TEXT("center_of_mass"),V3(B->GetCOMPosition()));Row->SetField(TEXT("velocity"),V3(B->GetUnrealWorldVelocity()));Row->SetField(TEXT("angular_velocity"),V3(B->GetUnrealWorldAngularVelocityInRadians()));Row->SetBoolField(TEXT("awake"),B->IsInstanceAwake());Row->SetBoolField(TEXT("finite"),!T.ContainsNaN());Bodies.Add(MakeShared<FJsonValueObject>(Row));
    }
    for(const FConstraintInstance* C:Mesh->Constraints)if(C)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("child"),C->ConstraintBone1.ToString());
        const FBodyInstance* B1=Mesh->GetBodyInstance(C->ConstraintBone1),*B2=Mesh->GetBodyInstance(C->ConstraintBone2);
        if(B1&&B2)
        {
            const FTransform T1=B1->GetUnrealWorldTransform(),T2=B2->GetUnrealWorldTransform();
            const float Scale=Mesh->GetComponentScale().GetAbsMin();
            const FVector P1=T1.TransformPosition(C->GetRefFrame(EConstraintFrame::Frame1).GetLocation()*Scale),P2=T2.TransformPosition(C->GetRefFrame(EConstraintFrame::Frame2).GetLocation()*Scale);
            Row->SetNumberField(TEXT("anchor_error_cm"),FVector::Dist(P1,P2));
            const FQuat Q1=T1.GetRotation()*C->GetRefFrame(EConstraintFrame::Frame1).GetRotation();
            const FQuat Q2=T2.GetRotation()*C->GetRefFrame(EConstraintFrame::Frame2).GetRotation();
            FQuat Relative=(Q2.Inverse()*Q1).GetNormalized(),Swing,Twist;
            Relative.ToSwingTwist(FVector::ForwardVector,Swing,Twist);
            if(Swing.W<0)Swing=Swing*-1.;
            FVector Axis;double Angle;Swing.ToAxisAndAngle(Axis,Angle);
            const double Degrees=FMath::RadiansToDegrees(Angle);
            const double TwistDegrees=FMath::RadiansToDegrees(Twist.GetTwistAngle(FVector::ForwardVector));
            // Same elliptical cone radius as Chaos FPBDJointUtilities::GetConeAngleLimit.
            // GetCurrentSwing1/2 instead project the whole quaternion onto each axis,
            // mixing forearm pronation into swing and falsely reporting overextension.
            const double ConeLimit=FMath::Sqrt(FMath::Square(C->GetAngularSwing1Limit()*Axis.Z)+FMath::Square(C->GetAngularSwing2Limit()*Axis.Y));
            Row->SetNumberField(TEXT("swing1"),Degrees*Axis.Z);Row->SetNumberField(TEXT("swing2"),Degrees*Axis.Y);Row->SetNumberField(TEXT("twist"),TwistDegrees);
            Row->SetNumberField(TEXT("angular_excess_deg"),FMath::Max3(0.,Degrees-ConeLimit,FMath::Abs(TwistDegrees)-C->GetAngularTwistLimit()));
        }
        Joints.Add(MakeShared<FJsonValueObject>(Row));
    }
    O->SetArrayField(TEXT("bodies"),Bodies);O->SetArrayField(TEXT("joints"),Joints);return Stringify(O);
}

bool USWCharacterRigValidationLibrary::ConfigureRagdollTestGameMode(UBlueprint* Blueprint)
{
    if(!ConfigureTestGameMode(Blueprint))return false;
    auto* Mode=Cast<AGameModeBase>(Blueprint->GeneratedClass->GetDefaultObject());if(!Mode)return false;
    if(Mode->PlayerStateClass==ABasePlayerState::StaticClass())return true;
    Mode->PlayerStateClass=ABasePlayerState::StaticClass();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::SkipSave);
    return Blueprint->Status!=BS_Error;
}

bool USWCharacterRigValidationLibrary::KillRagdollTestCharacter(ACharacter* Character,bool bDirectionalHit)
{
    ABasePlayer* Player=Cast<ABasePlayer>(Character);
    if(!Player||!Player->HasAuthority()||!Player->GetWorld()||Player->GetWorld()->WorldType!=EWorldType::PIE)return false;
    auto* ASC=Player->GetAbilitySystemComponent();auto* Health=Player->GetHealthComponent();if(!ASC||!Health||Health->IsDead())return false;
    if(!bDirectionalHit)ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(),0.f);
    else
    {
        auto* Source=Player->GetWorld()->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Source);Source->SetRootComponent(Root);Root->RegisterComponent();Source->SetActorLocation(Player->GetActorLocation()-FVector(150,0,0));Source->SetLifeSpan(1);
        auto Context=ASC->MakeEffectContext();Context.AddInstigator(Source,Source);
        FHitResult Hit;Hit.bBlockingHit=true;Hit.BoneName=TEXT("index_03_r");Hit.ImpactPoint=Player->GetMesh()->GetSocketLocation(Hit.BoneName);Hit.Location=Hit.ImpactPoint;Hit.TraceStart=Source->GetActorLocation();Hit.TraceEnd=Hit.ImpactPoint;Hit.ImpactNormal=-FVector::ForwardVector;Context.AddHitResult(Hit);
        auto* Effect=NewObject<UGameplayEffect>();Effect->DurationPolicy=EGameplayEffectDurationType::Instant;
        auto& Modifier=Effect->Modifiers.AddDefaulted_GetRef();Modifier.Attribute=UBaseAttributeSet::GetHealthAttribute();Modifier.ModifierOp=EGameplayModOp::Additive;Modifier.ModifierMagnitude=FScalableFloat(-FMath::Max(Health->GetHealth()+Health->GetMaxHealth(),1.f));
        ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Effect,Context,1));
    }
    return Health->IsDead();
}

void USWCharacterRigValidationLibrary::SetRagdollTestMovementBase(ACharacter* Character,UPrimitiveComponent* Base,FVector Velocity)
{
    if(!Character||!Base||Character->GetWorld()!=Base->GetWorld()||Character->GetWorld()->WorldType!=EWorldType::PIE)return;
    Base->ComponentVelocity=Velocity;Character->SetBase(Base);
}

ACharacter* USWCharacterRigValidationLibrary::SpawnRagdollTestNewLife(APlayerController* Controller,UBlueprint* Blueprint,FVector Location)
{
    auto* Previous=Controller?Cast<ABasePlayer>(Controller->GetPawn()):nullptr;
    if(!Previous||!Previous->HasAuthority()||Previous->GetWorld()->WorldType!=EWorldType::PIE||!Previous->GetHealthComponent()->IsDead())return nullptr;
    FSWRoomPlayerProgress Progress;Previous->CaptureRoomProgress(Progress);
    auto* Next=Cast<ABasePlayer>(SpawnTestCharacter(Previous->GetWorld(),Blueprint,Location));if(!Next)return nullptr;
    Next->GetHealthComponent()->BeginLifeInitialization();
    Previous->GetHealthComponent()->UninitializeFromAbilitySystem();
    Controller->Possess(Next);
    FString Error;
    if(!Next->RestoreProgressForNewLife(Progress,Error)||!Next->GetHealthComponent()->EndLifeInitialization())
    { UE_LOG(LogTemp,Error,TEXT("Ragdoll fixture life restore failed: %s"),*Error);return nullptr; }
    return Next;
}
