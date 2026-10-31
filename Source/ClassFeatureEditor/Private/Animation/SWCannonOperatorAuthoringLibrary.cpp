#include "Animation/SWCannonOperatorAuthoringLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimGraphNode_SWCannonOperator.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/PlayerController.h"
#include "Cannon.h"
#include "Engine/World.h"
#include "TimerManager.h"

bool USWCannonOperatorAuthoringLibrary::QueueOperatorBoard(APawn* Operator, ACannon* Cannon)
{
	if (!IsValid(Operator) || !IsValid(Cannon) || !Cannon->HasAuthority()
		|| Cannon->GetWorld()!=Operator->GetWorld() || Cannon->GetWorld()->WorldType!=EWorldType::PIE) return false;
	Cannon->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
		[Rider=TWeakObjectPtr<APawn>(Operator),Mount=TWeakObjectPtr<ACannon>(Cannon)]
		{
			if (Rider.IsValid() && Mount.IsValid()) Mount->Board(Rider.Get());
		}));
	return true;
}

bool USWCannonOperatorAuthoringLibrary::QueueOperatorDestroy(ACannon* Cannon)
{
	if (!IsValid(Cannon) || !Cannon->HasAuthority() || Cannon->GetWorld()->WorldType!=EWorldType::PIE) return false;
	Cannon->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
		[Mount=TWeakObjectPtr<ACannon>(Cannon)] { if (Mount.IsValid()) Mount->Destroy(); }));
	return true;
}

FString USWCannonOperatorAuthoringLibrary::DescribeOperatorController(APlayerController* PC)
{
	if (!PC) return TEXT("No controller");
	return FString::Printf(TEXT("Controller=%s Pawn=%s Ack=%s Local=%d State=%s"),
		*PC->GetPathName(), *GetPathNameSafe(PC->GetPawn()), *GetPathNameSafe(PC->AcknowledgedPawn),
		PC->IsLocalController(), *PC->GetStateName().ToString());
}

bool USWCannonOperatorAuthoringLibrary::InstallOperatorOverlay(UAnimBlueprint* BP)
{
	if (!BP) return false;
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph->GetFName()!=TEXT("AnimGraph")) continue;
		for (UEdGraphNode* N : Graph->Nodes) if (N->IsA<UAnimGraphNode_SWCannonOperator>()) return true;
		UAnimGraphNode_Root* Root=nullptr;
		for (UEdGraphNode* N : Graph->Nodes) if (auto* R=Cast<UAnimGraphNode_Root>(N)) { if (Root) return false; Root=R; }
		if (!Root) continue;
		UEdGraphPin* Input=Root->FindPin(TEXT("Result"),EGPD_Input);
		if (!Input || Input->LinkedTo.Num()!=1) return false;
		UEdGraphPin* Source=Input->LinkedTo[0];
		BP->Modify(); Graph->Modify(); Root->Modify();
		auto Add=[&](UClass* Class) { auto* Node=NewObject<UEdGraphNode>(Graph,Class,NAME_None,RF_Transactional); Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); return Node; };
		auto* ToCS=Add(UAnimGraphNode_LocalToComponentSpace::StaticClass());
		auto* Pose=Add(UAnimGraphNode_SWCannonOperator::StaticClass());
		auto* ToLocal=Add(UAnimGraphNode_ComponentToLocalSpace::StaticClass());
		ToCS->NodePosX=Root->NodePosX-600; Pose->NodePosX=Root->NodePosX-400; ToLocal->NodePosX=Root->NodePosX-200;
		Input->BreakAllPinLinks();
		const auto Connect=[&](UEdGraphPin* A, UEdGraphPin* B) { return A && B && Graph->GetSchema()->TryCreateConnection(A,B); };
		if (!Connect(Source,ToCS->FindPin(TEXT("LocalPose"),EGPD_Input))
			|| !Connect(ToCS->FindPin(TEXT("ComponentPose"),EGPD_Output),Pose->FindPin(TEXT("ComponentPose"),EGPD_Input))
			|| !Connect(Pose->FindPin(TEXT("Pose"),EGPD_Output),ToLocal->FindPin(TEXT("ComponentPose"),EGPD_Input))
			|| !Connect(ToLocal->FindPin(TEXT("Pose"),EGPD_Output),Input)) return false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		return BP->Status!=BS_Error;
	}
	return false;
}

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"

UStaticMesh* USWCannonOperatorAuthoringLibrary::BuildOperatorCannonPart(UStaticMesh* Source,const FString& PackagePath,bool bBarrel)
{
 if (!Source || !PackagePath.StartsWith(TEXT("/Game/Blueprints/Ship/Cannon/Mesh/Operator/"))) return nullptr;
 if (UStaticMesh* Existing=LoadObject<UStaticMesh>(nullptr,*(PackagePath+TEXT(".")+FPackageName::GetLongPackageAssetName(PackagePath)))) return Existing;
 FStaticMeshCompilingManager::Get().FinishCompilation({Source});
 const FMeshDescription* Original=Source->GetMeshDescription(0);
 if (!Original) return nullptr;
 FMeshDescription Description(*Original);
 const auto Positions=Description.GetVertexPositions();
 TArray<int32> Parent;Parent.Init(INDEX_NONE,Description.Vertices().GetArraySize());
 const auto Find=[&](int32 V) { while(Parent[V]!=V) {Parent[V]=Parent[Parent[V]];V=Parent[V];} return V; };
 TMap<FIntVector,int32> Weld;
 for (const FVertexID V:Description.Vertices().GetElementIDs())
 {
  const FVector3f P=Positions[V];const FIntVector Key(FMath::RoundToInt(P.X*1000.f),FMath::RoundToInt(P.Y*1000.f),FMath::RoundToInt(P.Z*1000.f));
  Parent[V.GetValue()]=V.GetValue();
  if (const int32* Other=Weld.Find(Key)) Parent[Find(V.GetValue())]=Find(*Other);
  else Weld.Add(Key,V.GetValue());
 }
 for (const FPolygonID P:Description.Polygons().GetElementIDs())
 {
  const auto Vertices=Description.GetPolygonVertices(P);
  for (int32 I=1;I<Vertices.Num();++I) Parent[Find(Vertices[I].GetValue())]=Find(Vertices[0].GetValue());
 }
 TMap<int32,FBox> Islands;
 for (const FVertexID V:Description.Vertices().GetElementIDs())
 {
  const int32 Root=Find(V.GetValue());
  if (!Islands.Contains(Root)) Islands.Add(Root,FBox(ForceInit));
  Islands[Root]+=FVector(Positions[V]);
 }
 TSet<int32> BarrelIslands;
 for (const auto& Pair:Islands) if (Pair.Value.GetSize().X>150.f && Pair.Value.Min.Z>0.f) BarrelIslands.Add(Pair.Key);
 if (BarrelIslands.Num()!=1) { UE_LOG(LogTemp,Error,TEXT("[CannonAuthor] Expected one connected upper barrel, found %d"),BarrelIslands.Num());return nullptr; }
 TArray<FPolygonID> Remove;int32 BarrelTriangles=0,TotalTriangles=0;
 for (const FPolygonID P:Description.Polygons().GetElementIDs())
 {
  const bool IsBarrel=BarrelIslands.Contains(Find(Description.GetPolygonVertices(P)[0].GetValue()));
  const int32 Count=Description.GetPolygonTriangles(P).Num();TotalTriangles+=Count;if(IsBarrel)BarrelTriangles+=Count;
  if (IsBarrel!=bBarrel) Remove.Add(P);
 }
 // Fail closed if a different source model is substituted; never silently cut through an island.
 if (BarrelTriangles!=2480 || TotalTriangles!=7100) {UE_LOG(LogTemp,Error,TEXT("[CannonAuthor] Source topology changed: barrel %d total %d"),BarrelTriangles,TotalTriangles);return nullptr;}
 for (const FPolygonID P:Remove) Description.DeletePolygon(P);
 FElementIDRemappings Remap;Description.Compact(Remap);
 UStaticMesh* Result=NewObject<UStaticMesh>(CreatePackage(*PackagePath),*FPackageName::GetLongPackageAssetName(PackagePath),RF_Public|RF_Standalone|RF_Transactional);
 Result->SetNumSourceModels(1);Result->GetStaticMaterials()=Source->GetStaticMaterials();
 Result->GetSourceModel(0).BuildSettings=Source->GetSourceModel(0).BuildSettings;
 Result->CreateMeshDescription(0,MoveTemp(Description));Result->CommitMeshDescription(0);
 Result->Build(false);FStaticMeshCompilingManager::Get().FinishCompilation({Result});
 Result->MarkPackageDirty();FAssetRegistryModule::AssetCreated(Result);
 return Result;
}

FString USWCannonOperatorAuthoringLibrary::DescribeOperatorRig(USkeletalMesh* Mesh)
{
 if (!Mesh) return TEXT("");
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();TArray<FTransform> CS=Ref.GetRefBonePose();FString Result;
 for(int32 I=0;I<Ref.GetNum();++I)
 {
  const int32 P=Ref.GetParentIndex(I);if(P>=0)CS[I]=CS[I]*CS[P];
  const FString Name=Ref.GetBoneName(I).ToString();
  if (Name.Contains(TEXT("hand")) || Name.Contains(TEXT("_01_")) || Name==TEXT("pelvis") || Name.Contains(TEXT("foot")))
   Result+=Name+TEXT(" ")+CS[I].ToHumanReadableString()+TEXT("\n");
 }
 return Result;
}

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

double USWCannonOperatorAuthoringLibrary::GetOperatorContactError(ACharacter* Operator,int32 Index)
{
 if (!Operator || Index<0 || Index>3) return -1.;
 const ACannon* Cannon=Cast<ACannon>(Operator->GetAttachParentActor());
 const USkeletalMeshComponent* Mesh=Operator->GetMesh();
 if (!Cannon || Cannon->GetRidingPlayer()!=Operator || !Mesh || !Mesh->GetSkeletalMeshAsset()) return -1.;
 const TCHAR* BoneNames[]={TEXT("foot_l"),TEXT("foot_r"),TEXT("hand_l"),TEXT("hand_r")};
 FVector Point=Mesh->GetSocketLocation(BoneNames[Index]);
 if (Index<2)
 {
  const FReferenceSkeleton& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
  int32 Bone=Ref.FindBoneIndex(BoneNames[Index]);if(Bone==INDEX_NONE)return -1.;
  FTransform BoneCS=Ref.GetRefBonePose()[Bone];
  while((Bone=Ref.GetParentIndex(Bone))!=INDEX_NONE)BoneCS=BoneCS*Ref.GetRefBonePose()[Bone];
  const FVector Up=Mesh->GetComponentTransform().InverseTransformVectorNoScale(Operator->GetActorUpVector());
  Point-=Mesh->GetComponentTransform().TransformVector(Up*FVector::DotProduct(Up,BoneCS.GetLocation()));
 }
 else
 {
  FVector Knuckles=FVector::ZeroVector;
  for(const TCHAR* Finger:{TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")})
   Knuckles+=Mesh->GetSocketLocation(FName(*FString::Printf(TEXT("%s_01_%s"),Finger,Index==2 ? TEXT("l") : TEXT("r"))))-Point;
  Point+=Knuckles*.1375;
 }
 return FVector::Distance(Point,Cannon->GetOperatorTarget(Index)->GetComponentLocation());
}

#include "Engine/StaticMeshSocket.h"

void USWCannonOperatorAuthoringLibrary::EnsureOperatorContactSockets(UStaticMesh* Mesh,const FTransform& LeftPalm,const FTransform& RightPalm)
{
 if (!Mesh) return;
 const FName Names[]={TEXT("Operator_LeftPalm"),TEXT("Operator_RightPalm")};
 const FTransform Frames[]={LeftPalm,RightPalm};
 for(int32 I=0;I<2;++I)
 {
  if(Mesh->FindSocket(Names[I]))continue;
  Mesh->Modify();
  UStaticMeshSocket* Socket=NewObject<UStaticMeshSocket>(Mesh,NAME_None,RF_Transactional);
  Socket->SocketName=Names[I];Socket->RelativeLocation=Frames[I].GetLocation();
  Socket->RelativeRotation=Frames[I].Rotator();Socket->RelativeScale=Frames[I].GetScale3D();
  Mesh->AddSocket(Socket);Mesh->MarkPackageDirty();
 }
}
