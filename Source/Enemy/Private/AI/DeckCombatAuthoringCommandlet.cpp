#include "AI/DeckCombatAuthoringCommandlet.h"

#if WITH_EDITOR
#include "AI/EnemyBehaviorSet.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Decorator/BTD_DeckCombatCondition.h"
#include "Service/BTS_MaintainDeckCombatFocus.h"
#include "Task/BTT_BroadcastEnemyAlarm.h"
#include "Task/BTT_DeckAttack.h"
#include "Task/BTT_MoveAroundDeckTarget.h"
#include "Task/BTT_MoveToDeckWaypoint.h"
#include "Task/BTT_SelectDeckWaypoint.h"
#include "Task/BTT_SetEnemyState.h"

namespace
{
	void Number(UObject* Object, const TCHAR* Name, double Value)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(Name);
		if (FEnumProperty* Enum = CastField<FEnumProperty>(Property))
			Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Object), int64(Value));
		else if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			void* Address = Numeric->ContainerPtrToValuePtr<void>(Object);
			if (Numeric->IsFloatingPoint()) Numeric->SetFloatingPointPropertyValue(Address, Value);
			else Numeric->SetIntPropertyValue(Address, int64(Value));
		}
		else if (FBoolProperty* Bool = CastField<FBoolProperty>(Property)) Bool->SetPropertyValue_InContainer(Object, Value != 0.0);
		else UE_LOG(LogTemp, Fatal, TEXT("Authoring property missing: %s.%s"), *Object->GetClass()->GetName(), Name);
	}
	template<typename T> T* Node(UBehaviorTree* Tree, UBTCompositeNode* Parent)
	{
		T* NewNode = NewObject<T>(Tree, NAME_None, RF_Transactional);
		FBTCompositeChild& Child = Parent->Children.AddDefaulted_GetRef();
		if constexpr (TIsDerivedFrom<T, UBTCompositeNode>::IsDerived) Child.ChildComposite = NewNode;
		else Child.ChildTask = NewNode;
		return NewNode;
	}
	void Condition(UBehaviorTree* Tree, UBTCompositeNode* Parent, EDeckCombatCondition Value, bool bAbort)
	{
		UBTD_DeckCombatCondition* Decorator = NewObject<UBTD_DeckCombatCondition>(Tree, NAME_None, RF_Transactional);
		Number(Decorator, TEXT("Condition"), int64(Value));
		Number(Decorator, TEXT("FlowAbortMode"), int64(bAbort ? EBTFlowAbortMode::LowerPriority : EBTFlowAbortMode::None));
		Parent->Children.Last().Decorators.Add(Decorator);
	}
	void Wait(UBehaviorTree* Tree, UBTCompositeNode* Parent, float Duration)
	{
		UBTTask_Wait* Task = Node<UBTTask_Wait>(Tree, Parent);
		Task->WaitTime = Duration; Task->RandomDeviation = 0.0f;
	}
	bool Save(UObject* Object)
	{
		Object->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Object->GetPackage(), Object,
			*FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	UBehaviorTree* NewTree(const FString& Name, UBlackboardData* Blackboard, bool bRebuild)
	{
		const FString Path = TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/DeckCombat/") + Name;
		UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *(Path + TEXT(".") + Name));
		// Preserve editor changes unless rebuilding these generated variants was explicitly requested.
		if (Tree && !bRebuild) return Tree;
		if (!Tree)
		{
			UPackage* Package = CreatePackage(*Path);
			Tree = NewObject<UBehaviorTree>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		}
		else if (Tree->BTGraph)
		{
			Tree->BTGraph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Tree->BTGraph = nullptr;
		}
		Tree->RootDecorators.Reset();
		Tree->RootDecoratorOps.Reset();
		Tree->BlackboardAsset = Blackboard;
		Tree->RootNode = NewObject<UBTComposite_Selector>(Tree, NAME_None, RF_Transactional);
		return Tree;
	}
	bool HasValidGraph(UBehaviorTree* Tree)
	{
		if (!Tree || !Tree->RootNode || !Tree->BTGraph) return false;
		for (UEdGraphNode* EdNode : Tree->BTGraph->Nodes)
		{
			UBehaviorTreeGraphNode* GraphNode = Cast<UBehaviorTreeGraphNode>(EdNode);
			if (!GraphNode || GraphNode->IsA<UBehaviorTreeGraphNode_Root>()) continue;
			if (!GraphNode->NodeInstance || GraphNode->HasErrors()) return false;
			for (UAIGraphNode* SubNode : GraphNode->SubNodes)
				if (!SubNode || !SubNode->NodeInstance || SubNode->HasErrors()) return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Loaded valid deck tree: %s (%d root branches)."),
			*Tree->GetPathName(), Tree->RootNode->Children.Num());
		return true;
	}
	bool FinalizeTree(UBehaviorTree* Tree)
	{
		if (Tree->BTGraph) return HasValidGraph(Tree);
		UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(Tree, TEXT("Behavior Tree"),
			UBehaviorTreeGraph::StaticClass(), GetDefault<UBehaviorTreeGraph>()->Schema));
		Tree->BTGraph = Graph;
		// AddSubNode updates the asset before assigning its decorator instance during reconstruction.
		// Freeze updates until all runtime nodes have their editor counterparts and links.
		Graph->LockUpdates();
		Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		Graph->OnCreated(); Graph->Initialize(); Graph->UnlockUpdates();
		return HasValidGraph(Tree) && Save(Tree);
	}
	UBehaviorTree* CombatTree(const FString& Name, UBlackboardData* Blackboard, float Distance, bool bAlarm, bool bRebuild)
	{
		UBehaviorTree* Tree = NewTree(Name, Blackboard, bRebuild);
		if (Tree->BTGraph) return HasValidGraph(Tree) ? Tree : nullptr;
		UBTCompositeNode* Root = Tree->RootNode;
		Root->Services.Add(NewObject<UBTS_MaintainDeckCombatFocus>(Tree, NAME_None, RF_Transactional));
		if (bAlarm)
		{
			UBTComposite_Sequence* Alarm = Node<UBTComposite_Sequence>(Tree, Root);
			Condition(Tree, Root, EDeckCombatCondition::AlarmPending, false);
			Node<UBTT_BroadcastEnemyAlarm>(Tree, Alarm);
		}
		UBTComposite_Sequence* Attack = Node<UBTComposite_Sequence>(Tree, Root);
		Condition(Tree, Root, EDeckCombatCondition::AttackReady, true);
		Node<UBTT_DeckAttack>(Tree, Attack);
		UBTComposite_Sequence* Recovery = Node<UBTComposite_Sequence>(Tree, Root);
		Condition(Tree, Root, EDeckCombatCondition::RecoveryPending, false);
		UBTComposite_Selector* Retry = Node<UBTComposite_Selector>(Tree, Recovery);
		UBTComposite_Sequence* RecoveryMove = Node<UBTComposite_Sequence>(Tree, Retry);
		Number(Node<UBTT_SelectDeckWaypoint>(Tree, RecoveryMove), TEXT("SelectionMode"), int64(EDeckWaypointSelectionMode::ReleaseLineOfSightReposition));
		Node<UBTT_MoveToDeckWaypoint>(Tree, RecoveryMove); Wait(Tree, RecoveryMove, 0.3f);
		Wait(Tree, Retry, 0.3f);
		UBTComposite_Sequence* Cooldown = Node<UBTComposite_Sequence>(Tree, Root);
		Condition(Tree, Root, EDeckCombatCondition::AttackCooldown, false);
		Number(Node<UBTT_MoveAroundDeckTarget>(Tree, Cooldown), TEXT("TargetDistance"), Distance);
		UBTComposite_Sequence* Position = Node<UBTComposite_Sequence>(Tree, Root);
		UBTT_SelectDeckWaypoint* Goal = Node<UBTT_SelectDeckWaypoint>(Tree, Position);
		Number(Goal, TEXT("SelectionMode"), int64(EDeckWaypointSelectionMode::Combat));
		Number(Goal, TEXT("TargetDistance"), Distance);
		Node<UBTT_MoveToDeckWaypoint>(Tree, Position); Wait(Tree, Position, 0.2f);
		Wait(Tree, Root, 0.3f);
		return FinalizeTree(Tree) ? Tree : nullptr;
	}
	UBehaviorTree* InvestigationTree(UBlackboardData* Blackboard, bool bRebuild)
	{
		UBehaviorTree* Tree = NewTree(TEXT("BT_DeckEnemy_Investigating"), Blackboard, bRebuild);
		if (Tree->BTGraph) return HasValidGraph(Tree) ? Tree : nullptr;
		UBTComposite_Sequence* Move = Node<UBTComposite_Sequence>(Tree, Tree->RootNode);
		Number(Node<UBTT_SelectDeckWaypoint>(Tree, Move), TEXT("SelectionMode"), int64(EDeckWaypointSelectionMode::Investigation));
		Number(Node<UBTT_MoveToDeckWaypoint>(Tree, Move), TEXT("bStopWhenAttackReady"), 0);
		Wait(Tree, Move, 1.0f);
		Number(Node<UBTT_SetEnemyState>(Tree, Move), TEXT("NewState"), int64(EEnemyAIState::Passive));
		UBTComposite_Sequence* NoPath = Node<UBTComposite_Sequence>(Tree, Tree->RootNode);
		Wait(Tree, NoPath, 0.3f);
		Number(Node<UBTT_SetEnemyState>(Tree, NoPath), TEXT("NewState"), int64(EEnemyAIState::Passive));
		return FinalizeTree(Tree) ? Tree : nullptr;
	}
}
#endif

UDeckCombatAuthoringCommandlet::UDeckCombatAuthoringCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}
int32 UDeckCombatAuthoringCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BB_EnemyBase.BB_EnemyBase"));
	if (!Blackboard) return 1;
	const bool bRebuild = FParse::Param(*Params, TEXT("RebuildGenerated"));
	UBehaviorTree* Melee = CombatTree(TEXT("BT_DeckMelee_Combat"), Blackboard, 150.0f, true, bRebuild);
	UBehaviorTree* Ranged = CombatTree(TEXT("BT_DeckRanged_Combat"), Blackboard, 500.0f, true, bRebuild);
	UBehaviorTree* Investigation = InvestigationTree(Blackboard, bRebuild);
	if (!Melee || !Ranged || !Investigation
		|| !CombatTree(TEXT("BT_DeckMelee_Combat_NoAlarm"), Blackboard, 150.0f, false, bRebuild)
		|| !CombatTree(TEXT("BT_DeckRanged_Combat_NoAlarm"), Blackboard, 500.0f, false, bRebuild)) return 1;
	for (const FString Role : { FString(TEXT("Melee")), FString(TEXT("Ranged")) })
	{
		const FString Name = TEXT("DA_Deck") + Role + TEXT("Enemy_AI");
		UEnemyBehaviorSet* Set = LoadObject<UEnemyBehaviorSet>(nullptr,
			*(TEXT("/Game/GameplayAbilitySystem/Enemy/DA/AI/") + Name + TEXT(".") + Name));
		if (!Set) return 1;
		Set->ConfigureEditorSubtree(EEnemyAIState::Combat, FGameplayTag::RequestGameplayTag(TEXT("AI.Behavior.Combat")), Role == TEXT("Melee") ? Melee : Ranged);
		Set->ConfigureEditorSubtree(EEnemyAIState::Investigating, FGameplayTag::RequestGameplayTag(TEXT("AI.Behavior.Investigating")), Investigation);
		if (!Save(Set)) return 1;
		UE_LOG(LogTemp, Display, TEXT("Configured %s with deck Combat/Investigating trees."), *Set->GetPathName());
	}
	UE_LOG(LogTemp, Display, TEXT("Deck Combat authoring completed. Cooldowns unchanged."));
	return 0;
#else
	return 1;
#endif
}
