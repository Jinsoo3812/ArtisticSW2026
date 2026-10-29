#include "Animation/SWNpcAuthoringLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Animation/SWNpcAnimInstance.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"

bool USWNpcAuthoringLibrary::ConfigureNpcLocomotionGraph(
    UAnimBlueprint* Blueprint, USkeleton* Skeleton, UBlendSpace* BlendSpace)
{
    if (!Blueprint || !Skeleton || !BlendSpace || BlendSpace->GetSkeleton() != Skeleton)
    {
        return false;
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UAnimGraphNode_BlendSpacePlayer* Player = nullptr;
    UAnimGraphNode_Root* Root = nullptr;
    UEdGraph* AnimGraph = nullptr;
    int32 PlayerCount = 0;
    int32 RootCount = 0;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph || Graph->GetFName() != TEXT("AnimGraph"))
        {
            continue;
        }
        AnimGraph = Graph;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimGraphNode_BlendSpacePlayer* Candidate = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
            {
                Player = Candidate;
                ++PlayerCount;
            }
            if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node))
            {
                Root = Candidate;
                ++RootCount;
            }
        }
    }
    if (!AnimGraph || PlayerCount != 1 || RootCount != 1)
    {
        return false;
    }
    UEdGraphPin* Pose = Player->FindPin(TEXT("Pose"), EGPD_Output);
    UEdGraphPin* Result = Root->FindPin(TEXT("Result"), EGPD_Input);
    if (!Pose || !Result || !AnimGraph->GetSchema())
    {
        return false;
    }

    Blueprint->Modify();
    AnimGraph->Modify();
    Player->Modify();
    Root->Modify();
    Blueprint->TargetSkeleton = Skeleton;
    Blueprint->ParentClass = USWNpcAnimInstance::StaticClass();
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
    Player->SetAnimationAsset(BlendSpace);
    for (int32 Index = 0; Index < Player->ShowPinForProperties.Num(); ++Index)
    {
        if (Player->ShowPinForProperties[Index].PropertyName == TEXT("X"))
        {
            Player->SetPinVisibility(true, Index);
            break;
        }
    }
    UK2Node_VariableGet* Speed = nullptr;
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        auto* Variable = Cast<UK2Node_VariableGet>(Node);
        if (Variable && Variable->VariableReference.GetMemberName() == TEXT("NpcGroundSpeed"))
        {
            Speed = Variable;
            break;
        }
    }
    if (!Speed)
    {
        Speed = NewObject<UK2Node_VariableGet>(AnimGraph);
        Speed->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(USWNpcAnimInstance, NpcGroundSpeed));
        AnimGraph->AddNode(Speed, true, false);
        Speed->CreateNewGuid();
        Speed->AllocateDefaultPins();
        Speed->NodePosX = Player->NodePosX - 240;
        Speed->NodePosY = Player->NodePosY;
    }
    UEdGraphPin* SpeedOutput = Speed->FindPin(TEXT("NpcGroundSpeed"), EGPD_Output);
    UEdGraphPin* SpeedInput = Player->FindPin(TEXT("X"), EGPD_Input);
    // Refresh/reconstruction can replace pins. Resolve the pose pins again.
    Pose = Player->FindPin(TEXT("Pose"), EGPD_Output);
    Result = Root->FindPin(TEXT("Result"), EGPD_Input);
    if (!SpeedOutput || !SpeedInput || !Pose || !Result)
    {
        return false;
    }
    if (!SpeedInput->LinkedTo.Contains(SpeedOutput) &&
        !AnimGraph->GetSchema()->TryCreateConnection(SpeedOutput, SpeedInput))
    {
        return false;
    }
    const bool bWasConnected = Result->LinkedTo.Contains(Pose);
    if (!bWasConnected && !AnimGraph->GetSchema()->TryCreateConnection(Pose, Result))
    {
        return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
    const UAnimBlueprintGeneratedClass* Generated = Cast<UAnimBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    const bool bValid = Blueprint->Status != BS_Error && Generated && Generated->GetAnimNodeProperties().Num() >= 2;
    UE_LOG(LogTemp, Display, TEXT("NPC locomotion graph %s: root previously connected=%d, compiled nodes=%d, valid=%d"),
        *Blueprint->GetPathName(), bWasConnected,
        Generated ? Generated->GetAnimNodeProperties().Num() : 0, bValid);
    return bValid;
}
