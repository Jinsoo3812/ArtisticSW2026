#include "Animation/GunnerCombatEditorLibrary.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Task/BTT_GunnerBossCombat.h"
#include "Task/BTT_SummonDeckEnemy.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"
#include "NiagaraSystem.h"
#include "Service/BTS_MaintainDeckCombatFocus.h"

int32 UGunnerCombatEditorLibrary::AddTaskFocus(UBehaviorTree* Tree)
{
	auto* Graph = Tree ? Cast<UBehaviorTreeGraph>(Tree->BTGraph) : nullptr;
	if (!Graph || !Tree->RootNode) return -1;
	Graph->LockUpdates();
	int32 Added = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		auto* TaskNode = Cast<UBehaviorTreeGraphNode>(Node);
		auto* Task = TaskNode ? Cast<UBTTaskNode>(TaskNode->NodeInstance) : nullptr;
		if (!Task) continue;
		bool bFound = false;
		for (UAIGraphNode* ServiceNode : TaskNode->Services)
			if (ServiceNode && Cast<UBTS_MaintainDeckCombatFocus>(ServiceNode->NodeInstance)) bFound = true;
		if (bFound) continue;
		auto* ServiceGraph = NewObject<UBehaviorTreeGraphNode_Service>(Graph, NAME_None, RF_Transactional);
		ServiceGraph->NodeInstance = NewObject<UBTS_MaintainDeckCombatFocus>(Tree, NAME_None, RF_Transactional);
		TaskNode->AddSubNode(ServiceGraph, Graph);
		++Added;
	}
	Graph->UnlockUpdates();
	Graph->UpdateAsset();
	Tree->MarkPackageDirty();
	return AuditTaskFocus(Tree) ? Added : -1;
}

bool UGunnerCombatEditorLibrary::AuditTaskFocus(UBehaviorTree* Tree)
{
	if (!Tree || !Tree->RootNode) return false;
	int32 Tasks = 0;
	bool bValid = true;
	TFunction<void(UBTCompositeNode*)> Visit = [&](UBTCompositeNode* Composite)
	{
		for (const auto& Child : Composite->Children)
		{
			if (Child.ChildComposite) Visit(Child.ChildComposite);
			if (const UBTTaskNode* Task = Child.ChildTask)
			{
				++Tasks;
				int32 FocusServices = 0;
				for (const UBTService* Service : Task->Services)
					if (Cast<UBTS_MaintainDeckCombatFocus>(Service)) ++FocusServices;
				bValid &= FocusServices == 1;
			}
		}
	};
	Visit(Tree->RootNode);
	UE_LOG(LogTemp, Display, TEXT("DECK_TASK_FOCUS_AUDIT Tree=%s Tasks=%d Valid=%d"), *Tree->GetName(), Tasks, bValid);
	return bValid && Tasks > 0;
}

bool UGunnerCombatEditorLibrary::SimplifyMuzzleFlash(UNiagaraSystem* System)
{
	if (!System || System->GetEmitterHandles().IsEmpty()) return false;
	System->Modify();
	int32 FlashIndex = 0;
	for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
		if (System->GetEmitterHandles()[Index].GetName().ToString().Contains(TEXT("Muzzle"))) { FlashIndex = Index; break; }
	for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
		System->GetEmitterHandles()[Index].SetIsEnabled(Index == FlashIndex, *System, false);
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("GUNNER_MUZZLE_FLASH_READY ActiveEmitter=%s"), *System->GetEmitterHandles()[FlashIndex].GetName().ToString());
	return true;
}

bool UGunnerCombatEditorLibrary::ConfigureGunnerSummonTree(UBehaviorTree* Tree, float SummonInterval)
{
	if (!Tree || !Tree->RootNode || !Tree->BlackboardAsset || !FMath::IsFinite(SummonInterval) || SummonInterval <= 0.f) return false;
	if (Cast<UBTComposite_SimpleParallel>(Tree->RootNode)) return AuditGunnerSummonTree(Tree, SummonInterval);
	if (Tree->RootNode->Children.Num() != 1 || !Cast<UBTT_GunnerBossCombat>(Tree->RootNode->Children[0].ChildTask)) return false;
	UBTTaskNode* Combat = Tree->RootNode->Children[0].ChildTask;
	auto* Parallel = NewObject<UBTComposite_SimpleParallel>(Tree, NAME_None, RF_Transactional);
	Parallel->FinishMode = EBTParallelMode::AbortBackground;
	Parallel->Children.AddDefaulted_GetRef().ChildTask = Combat;
	auto* Sequence = NewObject<UBTComposite_Sequence>(Tree, NAME_None, RF_Transactional);
	Parallel->Children.AddDefaulted_GetRef().ChildComposite = Sequence;
	auto* Wait = NewObject<UBTTask_Wait>(Tree, NAME_None, RF_Transactional);
	Wait->WaitTime = SummonInterval; Wait->RandomDeviation = 0.f;
	Sequence->Children.AddDefaulted_GetRef().ChildTask = Wait;
	auto* Summon = NewObject<UBTT_SummonDeckEnemy>(Tree, NAME_None, RF_Transactional);
	Summon->bWaitForBossIdle = true;
	Sequence->Children.AddDefaulted_GetRef().ChildTask = Summon;
	Tree->RootNode = Parallel;
	if (Tree->BTGraph) Tree->BTGraph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	auto* Graph = CastChecked<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(Tree, TEXT("Behavior Tree"),
		UBehaviorTreeGraph::StaticClass(), GetDefault<UBehaviorTreeGraph>()->Schema));
	Tree->BTGraph = Graph;
	Graph->LockUpdates(); Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
	for (UEdGraphNode* Node : Graph->Nodes)
		if (auto* Root = Cast<UBehaviorTreeGraphNode_Root>(Node)) Root->BlackboardAsset = Tree->BlackboardAsset;
	Graph->OnCreated(); Graph->Initialize(); Graph->UnlockUpdates();
	if (AddTaskFocus(Tree) < 0) return false;
	return AuditGunnerSummonTree(Tree, SummonInterval);
}

bool UGunnerCombatEditorLibrary::AuditGunnerSummonTree(UBehaviorTree* Tree, float SummonInterval)
{
	const auto* Parallel = Tree ? Cast<UBTComposite_SimpleParallel>(Tree->RootNode) : nullptr;
	if (!Parallel || Parallel->Children.Num() != 2 || !Cast<UBTT_GunnerBossCombat>(Parallel->Children[0].ChildTask)) return false;
	const auto* Background = Cast<UBTComposite_Sequence>(Parallel->Children[1].ChildComposite);
	if (!Background || Background->Children.Num() != 2) return false;
	const auto* Wait = Cast<UBTTask_Wait>(Background->Children[0].ChildTask);
	const auto* Summon = Cast<UBTT_SummonDeckEnemy>(Background->Children[1].ChildTask);
	return Wait && Summon && FMath::IsNearlyEqual(Wait->WaitTime, SummonInterval) && Wait->RandomDeviation == 0.f
		&& Summon->bWaitForBossIdle && Parallel->FinishMode == EBTParallelMode::AbortBackground && AuditTaskFocus(Tree);
}
