#include "Animation/SWCharacterRigValidationLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Animation/MotionMatchingAnimInstance.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "SwimmingComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Knot.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
using FObject = TSharedPtr<FJsonObject>;
FObject Object() { return MakeShared<FJsonObject>(); }
FString Json(const FObject& O)
{
    FString Text;
    FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    return Text;
}
TSharedPtr<FJsonValue> Value(const FObject& O) { return MakeShared<FJsonValueObject>(O); }
TArray<TSharedPtr<FJsonValue>> Vector(const FVector& V)
{
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
FObject Transform(const FTransform& T)
{
    auto O=Object();O->SetArrayField(TEXT("position"),Vector(T.GetTranslation()));
    O->SetArrayField(TEXT("scale"),Vector(T.GetScale3D()));
    const FQuat Q=T.GetRotation();
    O->SetArrayField(TEXT("rotation"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
    return O;
}
TArray<FTransform> ComponentRef(const USkeletalMesh* Mesh)
{
    const auto& R=Mesh->GetRefSkeleton();TArray<FTransform> Result=R.GetRefBonePose();
    for(int32 I=1;I<Result.Num();++I)Result[I]*=Result[R.GetParentIndex(I)];
    return Result;
}
}

FString USWCharacterRigValidationLibrary::DescribeGraph(UAnimBlueprint* BP)
{
    auto O=Object();if(!BP)return Json(O);
    O->SetStringField(TEXT("asset"),BP->GetPathName());
    O->SetStringField(TEXT("skeleton"),GetPathNameSafe(BP->TargetSkeleton));
    O->SetStringField(TEXT("preview_mesh"),GetPathNameSafe(BP->GetPreviewMesh(false)));
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(UEdGraph* G:Graphs)for(UEdGraphNode* N:G->Nodes)
    {
        auto R=Object();R->SetStringField(TEXT("name"),N->GetName());R->SetStringField(TEXT("guid"),N->NodeGuid.ToString());
        R->SetStringField(TEXT("graph"),G->GetName());R->SetStringField(TEXT("class"),N->GetClass()->GetName());R->SetStringField(TEXT("comment"),N->NodeComment);
        if(auto* P=FindFProperty<FStructProperty>(N->GetClass(),TEXT("Node")))
        {
            FString Text;P->ExportText_InContainer(0,Text,N,nullptr,N,PPF_None);R->SetStringField(TEXT("settings"),Text);
        }
        TArray<TSharedPtr<FJsonValue>> Pins;
        for(UEdGraphPin* P:N->Pins)
        {
            auto Pin=Object();Pin->SetStringField(TEXT("name"),P->PinName.ToString());Pin->SetBoolField(TEXT("input"),P->Direction==EGPD_Input);
            Pin->SetStringField(TEXT("type"),GetPathNameSafe(P->PinType.PinSubCategoryObject.Get()));Pin->SetStringField(TEXT("default"),P->DefaultValue);
            TArray<TSharedPtr<FJsonValue>> Links;
            for(UEdGraphPin* L:P->LinkedTo)Links.Add(MakeShared<FJsonValueString>(L->GetOwningNode()->GetName()+TEXT(".")+L->PinName.ToString()));
            Pin->SetArrayField(TEXT("links"),Links);Pins.Add(Value(Pin));
        }
        R->SetArrayField(TEXT("pins"),Pins);Rows.Add(Value(R));
    }
    O->SetArrayField(TEXT("nodes"),Rows);return Json(O);
}

bool USWCharacterRigValidationLibrary::BypassFixedBoneCorrections(UAnimBlueprint* BP,const TArray<FName>& Names)
{
    if(!BP||Names.IsEmpty())return false;
    struct FBypass { UEdGraphNode* Node; UEdGraphPin* Input; UEdGraphPin* Output; };
    TArray<FBypass> Plan;TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);TSet<UEdGraphNode*> CorrectionReroutes;
    for(FName Name:Names)
    {
        UAnimGraphNode_ModifyBone* Match=nullptr;
        for(UEdGraph* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(N->GetFName()==Name)
        { if(Match)return false;Match=Cast<UAnimGraphNode_ModifyBone>(N);if(!Match)return false; }
        if(!Match)return false;
        auto* Input=Match->FindPin(TEXT("ComponentPose"),EGPD_Input);auto* Output=Match->FindPin(TEXT("Pose"),EGPD_Output);
        if(!Input||!Output||Input->LinkedTo.Num()!=1||Output->LinkedTo.IsEmpty())return false;
        // A correction may fade in/out with alpha, but its authored transform
        // must be fixed. Never bypass a driven aim/contact transform here.
        for(auto* P:Match->Pins)if(P->Direction==EGPD_Input&&!P->LinkedTo.IsEmpty()
            &&(P->PinName==TEXT("Rotation")||P->PinName==TEXT("Translation")||P->PinName==TEXT("Scale")))return false;
        Plan.Add({Match,Input,Output});
        TArray<UEdGraphNode*> Pending;
        for(auto* Pin:Match->Pins)if(Pin!=Input&&Pin->Direction==EGPD_Input)for(auto* Link:Pin->LinkedTo)Pending.Add(Link->GetOwningNode());
        while(!Pending.IsEmpty())
        {
            auto* N=Pending.Pop();if(!N->IsA<UK2Node_Knot>()||CorrectionReroutes.Contains(N))continue;
            CorrectionReroutes.Add(N);
            for(auto* Pin:N->Pins)if(Pin->Direction==EGPD_Input)for(auto* Link:Pin->LinkedTo)Pending.Add(Link->GetOwningNode());
        }
    }
    BP->Modify();
    for(const auto& Item:Plan)
    {
        UEdGraphPin* Source=Item.Input->LinkedTo[0];auto Destinations=Item.Output->LinkedTo;
        auto* G=Item.Node->GetGraph();G->Modify();Item.Node->Modify();
        Item.Input->BreakAllPinLinks();Item.Output->BreakAllPinLinks();
        for(auto* Dest:Destinations)if(!G->GetSchema()->TryCreateConnection(Source,Dest))return false;
        FBlueprintEditorUtils::RemoveNode(BP,Item.Node,true);
    }
    bool bRemoved=true;
    while(bRemoved)
    {
        bRemoved=false;
        for(auto It=CorrectionReroutes.CreateIterator();It;++It)
        {
            auto* Node=*It;bool bUsed=false;
            for(auto* Pin:Node->Pins)if(Pin->Direction==EGPD_Output&&!Pin->LinkedTo.IsEmpty())bUsed=true;
            if(!bUsed){FBlueprintEditorUtils::RemoveNode(BP,Node,true);It.RemoveCurrent();bRemoved=true;}
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status!=BS_Error;
}

FString USWCharacterRigValidationLibrary::DescribeMesh(USkeletalMesh* Mesh)
{
    auto O=Object();if(!Mesh)return Json(O);
    O->SetStringField(TEXT("asset"),Mesh->GetPathName());O->SetStringField(TEXT("skeleton"),GetPathNameSafe(Mesh->GetSkeleton()));
    O->SetStringField(TEXT("physics"),GetPathNameSafe(Mesh->GetPhysicsAsset()));
    const auto& R=Mesh->GetRefSkeleton();auto CS=ComponentRef(Mesh);TArray<TSharedPtr<FJsonValue>> Bones,Sockets,Bodies;
    for(int32 I=0;I<R.GetNum();++I)
    {auto B=Transform(CS[I]);B->SetStringField(TEXT("name"),R.GetBoneName(I).ToString());B->SetNumberField(TEXT("parent"),R.GetParentIndex(I));Bones.Add(Value(B));}
    for(const auto* S:Mesh->GetActiveSocketList())if(S)
    {auto X=Transform(S->GetSocketLocalTransform());X->SetStringField(TEXT("name"),S->SocketName.ToString());X->SetStringField(TEXT("bone"),S->BoneName.ToString());Sockets.Add(Value(X));}
    if(auto* PA=Mesh->GetPhysicsAsset())for(const USkeletalBodySetup* B:PA->SkeletalBodySetups)if(B)
    {auto X=Object();X->SetStringField(TEXT("bone"),B->BoneName.ToString());X->SetBoolField(TEXT("bone_exists"),R.FindBoneIndex(B->BoneName)!=INDEX_NONE);X->SetNumberField(TEXT("shapes"),B->AggGeom.GetElementCount());Bodies.Add(Value(X));}
    O->SetArrayField(TEXT("bones"),Bones);O->SetArrayField(TEXT("sockets"),Sockets);O->SetArrayField(TEXT("bodies"),Bodies);return Json(O);
}

FString USWCharacterRigValidationLibrary::SnapshotCharacter(ACharacter* Character)
{
    auto O=Object();if(!IsValid(Character))return Json(O);
    auto* C=Character->GetMesh();auto* Mesh=C->GetSkeletalMeshAsset();if(!Mesh)return Json(O);
    auto* Anim=C->GetAnimInstance();
    O->SetNumberField(TEXT("time"),Character->GetWorld()->GetTimeSeconds());O->SetStringField(TEXT("mesh"),Mesh->GetPathName());
    O->SetStringField(TEXT("anim_class"),Anim?Anim->GetClass()->GetPathName():TEXT(""));O->SetObjectField(TEXT("actor"),Transform(Character->GetActorTransform()));
    O->SetObjectField(TEXT("mesh_world"),Transform(C->GetComponentTransform()));
    O->SetArrayField(TEXT("velocity"),Vector(Character->GetVelocity()));
    O->SetNumberField(TEXT("movement_mode"),Character->GetCharacterMovement()->MovementMode);
    O->SetNumberField(TEXT("net_mode"),Character->GetNetMode());O->SetBoolField(TEXT("locally_controlled"),Character->IsLocallyControlled());
    if(auto* Player=Cast<ABasePlayer>(Character))
    {
        O->SetBoolField(TEXT("can_mutate_life"),Player->CanMutateLifeGameplay());O->SetBoolField(TEXT("sprint_held"),Player->bSprintInputHeld);
        O->SetBoolField(TEXT("attacking"),Player->bIsAttacking);O->SetBoolField(TEXT("dodging"),Player->bIsDodging);O->SetBoolField(TEXT("hit_reacting"),Player->bIsHitReacting);
        if(auto* Swim=Player->GetSwimmingComponent()){O->SetBoolField(TEXT("shallow_water"),Swim->IsInShallowWater());O->SetBoolField(TEXT("swimming"),Swim->IsCustomSwimming());}
        if(auto* PC=Cast<ABasePlayerController>(Player->GetController()))O->SetNumberField(TEXT("life_phase"),static_cast<int32>(PC->DeathFlowState.Phase));
    }
    if(auto* Presentation=Cast<UMotionMatchingAnimInstance>(Anim))
    {
        O->SetBoolField(TEXT("direct_override"),Presentation->GetThreadSafeShouldOverrideMotionMatching());
        O->SetStringField(TEXT("direct_animation"),GetPathNameSafe(Presentation->GetThreadSafeStateControllerSelectedAnimation()));
    }
    const auto& Ref=Mesh->GetRefSkeleton();const auto& CS=C->GetComponentSpaceTransforms();TArray<TSharedPtr<FJsonValue>> Bones,MM;
    for(int32 I=0;I<CS.Num();++I)
    {
        auto B=Transform(CS[I]);B->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());const int32 P=Ref.GetParentIndex(I);
        const FTransform Local=P==INDEX_NONE?CS[I]:CS[I].GetRelativeTransform(CS[P]);B->SetObjectField(TEXT("local"),Transform(Local));
        B->SetNumberField(TEXT("bind_length"),Ref.GetRefBonePose()[I].GetTranslation().Size());B->SetBoolField(TEXT("finite"),!CS[I].ContainsNaN());Bones.Add(Value(B));
    }
    if(Anim)for(TFieldIterator<FStructProperty> It(Anim->GetClass());It;++It)if(It->Struct==FAnimNode_MotionMatching::StaticStruct())
    {
        const auto* N=It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);const auto& S=N->GetMotionMatchingState().SearchResult;
        const bool bValid=S.SelectedAnim&&S.SelectedDatabase;
        auto M=Object();M->SetStringField(TEXT("node"),It->GetName());M->SetNumberField(TEXT("weight"),N->GetCachedBlendWeight());M->SetBoolField(TEXT("valid"),bValid);
        M->SetNumberField(TEXT("stack_size"),N->AnimPlayers.Num());M->SetStringField(TEXT("database"),GetPathNameSafe(S.SelectedDatabase));
        if(bValid){M->SetStringField(TEXT("animation"),GetPathNameSafe(S.SelectedAnim));M->SetNumberField(TEXT("asset_time"),S.SelectedTime);M->SetNumberField(TEXT("cost"),S.SearchCost);}
        MM.Add(Value(M));
    }
    if(auto* State=Character->FindComponentByClass<ULocomotionAnimStateComponent>())
    {O->SetNumberField(TEXT("locomotion_state"),static_cast<int32>(State->CurrentState));O->SetNumberField(TEXT("ground_speed"),State->GroundSpeed);O->SetBoolField(TEXT("sprinting"),State->bIsSprinting);}
    O->SetArrayField(TEXT("bones"),Bones);O->SetArrayField(TEXT("motion_matching"),MM);return Json(O);
}

bool USWCharacterRigValidationLibrary::EnsureRigPhysics(USkeletalMesh* Mesh)
{
    if(!Mesh)return false;
    const bool bWoman=Mesh->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChWomanUE5/"));
    if(!bWoman&&!Mesh->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChManUE5/")))return false;
    // A reimport can retain mesh sockets while the lookup cache is stale.
    // Inspect serialized entries directly and remove only identical duplicates.
    auto& MeshSockets=Mesh->GetMeshOnlySocketList();TMap<FName,USkeletalMeshSocket*> First;
    TArray<int32> Duplicates;
    for(int32 I=0;I<MeshSockets.Num();++I)if(auto* Socket=MeshSockets[I].Get())
    {
        if(auto** Existing=First.Find(Socket->SocketName))
        {
            if((*Existing)->BoneName!=Socket->BoneName||!(*Existing)->GetSocketLocalTransform().Equals(Socket->GetSocketLocalTransform(),1.e-6f))return false;
            Duplicates.Add(I);
        }
        else First.Add(Socket->SocketName,Socket);
    }
    for(int32 I=Duplicates.Num()-1;I>=0;--I){MeshSockets.RemoveAt(Duplicates[I]);Mesh->MarkPackageDirty();}
    TSet<FName> ExistingNames;
    for(const auto* Socket:Mesh->GetActiveSocketList())if(Socket)ExistingNames.Add(Socket->SocketName);
    if(auto* Source=LoadObject<USkeletalMesh>(nullptr,bWoman?TEXT("/Game/jiwon/Characters/SKM_Player_Woman.SKM_Player_Woman"):TEXT("/Game/jiwon/Characters/SKM_Player_Man.SKM_Player_Man")))
    {
        for(const auto* Socket:Source->GetActiveSocketList())if(Socket&&!ExistingNames.Contains(Socket->SocketName)
            &&Mesh->GetRefSkeleton().FindBoneIndex(Socket->BoneName)!=INDEX_NONE)
        {
            auto* Copy=DuplicateObject<USkeletalMeshSocket>(Socket,Mesh);
            MeshSockets.Add(Copy);ExistingNames.Add(Socket->SocketName);Mesh->MarkPackageDirty();
        }
    }
    Mesh->RebuildSocketMap();
    if(Mesh->GetPhysicsAsset())return true;
    const FString Name=bWoman?TEXT("PA_ChWoman_UE5"):TEXT("PA_ChMan_UE5");
    const FString Package=(bWoman?TEXT("/Game/jiwon/Characters/ChWomanUE5/"):TEXT("/Game/jiwon/Characters/ChManUE5/"))+Name;
    auto* PA=LoadObject<UPhysicsAsset>(nullptr,*(Package+TEXT(".")+Name));
    if(!PA)
    {
        PA=NewObject<UPhysicsAsset>(CreatePackage(*Package),FName(*Name),RF_Public|RF_Standalone|RF_Transactional);
        FPhysAssetCreateParams Params;Params.MinBoneSize=12.f;FText Error;
        if(!FPhysicsAssetUtils::CreateFromSkeletalMesh(PA,Mesh,Params,Error,true,false))
        {UE_LOG(LogTemp,Error,TEXT("[ChManRig] Physics fit: %s"),*Error.ToString());return false;}
        FAssetRegistryModule::AssetCreated(PA);PA->MarkPackageDirty();
    }
    Mesh->SetPhysicsAsset(PA);Mesh->MarkPackageDirty();return true;
}

bool USWCharacterRigValidationLibrary::ConfigureCharacter(UBlueprint* BP,USkeletalMesh* Mesh,UAnimBlueprint* Animation)
{
    if(!BP||!Mesh||!Animation||!BP->GeneratedClass||!Animation->GeneratedClass)return false;
    auto* Character=Cast<ACharacter>(BP->GeneratedClass->GetDefaultObject());if(!Character)return false;
    BP->Modify();auto* C=Character->GetMesh();C->Modify();
    Animation->Modify();Animation->SetPreviewMesh(Mesh);
    C->SetSkeletalMeshAsset(Mesh);C->SetAnimInstanceClass(Animation->GeneratedClass);
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);return BP->Status!=BS_Error;
}

ACharacter* USWCharacterRigValidationLibrary::SpawnTestCharacter(UWorld* World,UBlueprint* BP,FVector Location)
{
    if(!World||World->WorldType!=EWorldType::PIE||!BP||!BP->GeneratedClass
        ||!BP->GeneratedClass->IsChildOf(ACharacter::StaticClass()))return nullptr;
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<ACharacter>(BP->GeneratedClass,Location,FRotator::ZeroRotator,Params);
}

bool USWCharacterRigValidationLibrary::ConfigureTestGameMode(UBlueprint* BP)
{
    if(!BP||!BP->GeneratedClass||!BP->GetPathName().StartsWith(TEXT("/Game/Developers/ChManRigValidation/")))return false;
    auto* Mode=Cast<AGameModeBase>(BP->GeneratedClass->GetDefaultObject());if(!Mode)return false;
    auto* Controller=LoadClass<APlayerController>(nullptr,TEXT("/Game/Blueprints/Player/BP_BasePlayerController.BP_BasePlayerController_C"));
    if(!Controller||!Controller->IsChildOf(ABasePlayerController::StaticClass()))return false;
    if(Mode->PlayerControllerClass==Controller&&!Mode->DefaultPawnClass)return true;
    BP->Modify();Mode->Modify();Mode->PlayerControllerClass=Controller;Mode->DefaultPawnClass=nullptr;
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::SkipSave);
    return BP->Status!=BS_Error;
}

bool USWCharacterRigValidationLibrary::ConfigureFinalLimbMarkers(UAnimBlueprint* BP,USkeletalMesh* Mesh)
{
    if(!BP||!Mesh||BP->TargetSkeleton!=Mesh->GetSkeleton()
        ||(!BP->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChManUE5/"))&&!BP->GetPathName().StartsWith(TEXT("/Game/jiwon/Characters/ChWomanUE5/"))))return false;
    UEdGraph* Graph=nullptr;
    for(UEdGraph* G:BP->FunctionGraphs)if(G->GetFName()==TEXT("AnimGraph"))Graph=G;
    if(!Graph)return false;
    int32 Copies=0;UAnimGraphNode_Root* Root=nullptr;
    for(UEdGraphNode* N:Graph->Nodes){if(N->IsA<UAnimGraphNode_CopyBone>())++Copies;if(auto* R=Cast<UAnimGraphNode_Root>(N))Root=R;}
    if(Copies!=0&&Copies!=4)return false;
    if(!Root)return false;
    if(Copies==0)
    {
        if(Graph->Nodes.Num()!=1)return false;
        BP->Modify();Graph->Modify();
        auto Finish=[&](UEdGraphNode* N,int32 X)
        {Graph->AddNode(N,false,false);N->CreateNewGuid();N->PostPlacedNewNode();N->AllocateDefaultPins();N->NodePosX=X;N->NodePosY=0;};
        auto* Input=NewObject<UAnimGraphNode_LinkedInputPose>(Graph,NAME_None,RF_Transactional);Finish(Input,-1500);
        auto* ToCS=NewObject<UAnimGraphNode_LocalToComponentSpace>(Graph,NAME_None,RF_Transactional);Finish(ToCS,-1300);
        auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){return A&&B&&Graph->GetSchema()->TryCreateConnection(A,B);};
        if(!Link(Input->FindPin(TEXT("Pose"),EGPD_Output),ToCS->FindPin(TEXT("LocalPose"),EGPD_Input)))return false;
        UEdGraphPin* Previous=ToCS->FindPin(TEXT("ComponentPose"),EGPD_Output);
        const FName Sources[]={TEXT("foot_l"),TEXT("foot_r"),TEXT("hand_l"),TEXT("hand_r")};
        const FName Targets[]={TEXT("ik_foot_l"),TEXT("ik_foot_r"),TEXT("ik_hand_l"),TEXT("ik_hand_r")};
        for(int32 I=0;I<4;++I)
        {
            if(Mesh->GetRefSkeleton().FindBoneIndex(Sources[I])==INDEX_NONE||Mesh->GetRefSkeleton().FindBoneIndex(Targets[I])==INDEX_NONE)return false;
            auto* Copy=NewObject<UAnimGraphNode_CopyBone>(Graph,NAME_None,RF_Transactional);
            Copy->Node.SourceBone=FBoneReference(Sources[I]);Copy->Node.TargetBone=FBoneReference(Targets[I]);
            Copy->Node.bCopyTranslation=true;Copy->Node.bCopyRotation=true;Copy->Node.bCopyScale=false;Copy->Node.ControlSpace=BCS_ComponentSpace;
            Finish(Copy,-1100+I*220);Copy->NodeComment=TEXT("Final solved limb marker; no skin weights");
            if(!Link(Previous,Copy->FindPin(TEXT("ComponentPose"),EGPD_Input)))return false;
            Previous=Copy->FindPin(TEXT("Pose"),EGPD_Output);
        }
        auto* ToLocal=NewObject<UAnimGraphNode_ComponentToLocalSpace>(Graph,NAME_None,RF_Transactional);Finish(ToLocal,-200);
        if(!Link(Previous,ToLocal->FindPin(TEXT("ComponentPose"),EGPD_Input))||!Link(ToLocal->FindPin(TEXT("Pose"),EGPD_Output),Root->FindPin(TEXT("Result"),EGPD_Input)))return false;
        Root->NodePosX=20;FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    }
    FKismetEditorUtilities::CompileBlueprint(BP);if(BP->Status==BS_Error)return false;
    BP->SetPreviewMesh(Mesh);Mesh->Modify();Mesh->SetPostProcessAnimBlueprint(TSubclassOf<UAnimInstance>(BP->GeneratedClass.Get()));Mesh->MarkPackageDirty();return true;
}
