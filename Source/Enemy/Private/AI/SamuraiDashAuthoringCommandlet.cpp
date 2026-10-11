#include "AI/SamuraiDashAuthoringCommandlet.h"

#if WITH_EDITOR
#include "BaseGameplayTags.h"
#include "AI/EnemyBehaviorSet.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BossAI/ShipBossEnemy.h"
#include "BossAI/SamuraiDashRecoveryComponent.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Decorator/BTD_CanActivateAbilityByTag.h"
#include "Decorator/BTD_SamuraiDashRecovery.h"
#include "Task/BTT_ActivateBossAbility.h"
#include "Task/BTT_RepositionSamuraiAfterDashFailure.h"
#include "Task/BTT_SelectSamuraiDashPoint.h"

namespace
{
	bool RestoreBlackboardBindings(UBehaviorTree* Tree)
	{
		auto* Blackboard = LoadObject<UBlackboardData>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BB_RogueBoss.BB_RogueBoss"));
		if (!Blackboard || Blackboard->GetKeyID(TEXT("DestinationLocation")) == FBlackboard::InvalidKey) return false;
		Tree->BlackboardAsset = Blackboard;
		for (UEdGraphNode* Node : Tree->BTGraph->Nodes)
			if (auto* Root = Cast<UBehaviorTreeGraphNode_Root>(Node)) Root->BlackboardAsset = Blackboard;
		TFunction<void(UBTCompositeNode*)> Visit = [&](UBTCompositeNode* Parent)
		{
			for (auto& Child : Parent->Children)
			{
				if (Child.ChildComposite) Visit(Child.ChildComposite);
				UObject* Task = Child.ChildTask;
				if (!Task) continue;
				if (Task->IsA<UBTT_SelectBossDestinationPoint>())
				{
					auto* P = FindFProperty<FStructProperty>(Task->GetClass(), TEXT("BlackboardKey"));
					P->ContainerPtrToValuePtr<FBlackboardKeySelector>(Task)->SelectedKeyName = TEXT("DestinationLocation");
				}
				if (Task->IsA<UBTT_ActivateBossAbility>())
				{
					auto* P = FindFProperty<FStructProperty>(Task->GetClass(), TEXT("DestinationPointKey"));
					P->ContainerPtrToValuePtr<FBlackboardKeySelector>(Task)->SelectedKeyName = TEXT("DestinationLocation");
				}
			}
		};
		Visit(Tree->RootNode);
		CastChecked<UBehaviorTreeGraph>(Tree->BTGraph)->UpdateBlackboardChange();
		return true;
	}
	bool RebuildEditorGraph(UBehaviorTree* Tree)
	{
		if (Tree->BTGraph)
		{
			Tree->BTGraph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Tree->BTGraph = nullptr;
		}
		auto* Graph = Cast<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(Tree, TEXT("Behavior Tree"),
			UBehaviorTreeGraph::StaticClass(), GetDefault<UBehaviorTreeGraph>()->Schema));
		Tree->BTGraph = Graph;
		Graph->LockUpdates(); Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		// Root placement chooses the engine default Blackboard. Restore Samurai's contract
		// and any invalidated key names before reconstructing runtime/editor nodes.
		if (!RestoreBlackboardBindings(Tree)) return false;
		Graph->OnCreated(); Graph->Initialize(); Graph->UnlockUpdates(); Graph->UpdateAsset();
		return true;
	}
	bool SaveTree(UBehaviorTree* Tree)
	{
		Tree->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Tree->GetPackage(), Tree,
			*FPackageName::LongPackageNameToFilename(Tree->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	void SetBool(UObject* Object, const FName Name, bool Value)
	{
		FBoolProperty* P = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
		check(P);
		P->SetPropertyValue_InContainer(Object, Value);
	}
	void Gate(UBehaviorTree* Tree, FBTCompositeChild& Child, ESamuraiDashRecoveryCondition Condition)
	{
		auto* D = NewObject<UBTD_SamuraiDashRecovery>(Tree, NAME_None, RF_Transactional);
		D->Configure(Condition);
		Child.Decorators.Add(D);
		// Existing Samurai connections are conjunctions. Empty ops preserve implicit AND.
		Child.DecoratorOps.Reset();
	}
	bool HasGate(const FBTCompositeChild& Child, ESamuraiDashRecoveryCondition Condition)
	{
		for (const auto& D : Child.Decorators)
			if (const auto* GateNode = Cast<UBTD_SamuraiDashRecovery>(D); GateNode && GateNode->GetCondition() == Condition) return true;
		return false;
	}
	FGameplayTag AbilityTag(UBTCompositeNode* Branch)
	{
		if (Branch) for (const auto& Child : Branch->Children)
			if (const auto* Task = Cast<UBTT_ActivateBossAbility>(Child.ChildTask)) return Task->GetAbilityAssetTag();
		return {};
	}
	bool Audit(UBehaviorTree* Tree)
	{
		if (!Tree || !Tree->RootNode || Tree->RootNode->Children.Num() != 2 || !Tree->BTGraph) return false;
		if (!Tree->BlackboardAsset || Tree->BlackboardAsset->GetPathName() != TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BB_RogueBoss.BB_RogueBoss")
			|| Tree->BlackboardAsset->GetKeyID(TEXT("DestinationLocation")) == FBlackboard::InvalidKey) return false;
		const auto& Reposition = Tree->RootNode->Children[0];
		const auto& Normal = Tree->RootNode->Children[1];
		if (!Cast<UBTT_RepositionSamuraiAfterDashFailure>(Reposition.ChildTask)
			|| !HasGate(Reposition, ESamuraiDashRecoveryCondition::RepositionReady)
			|| !HasGate(Normal, ESamuraiDashRecoveryCondition::AllowNormalBehavior) || !Normal.ChildComposite) return false;
		int32 Single = 0, Chain = 0;
		for (const auto& Child : Normal.ChildComposite->Children)
		{
			const FGameplayTag Tag = AbilityTag(Child.ChildComposite);
			if (Tag != GameplayAbility_Boss_DashSlash && Tag != GameplayAbility_Boss_ChainSlashDash)
			{
				if (!HasGate(Child, ESamuraiDashRecoveryCondition::AllowNormalBehavior)) return false;
				continue;
			}
			if (!HasGate(Child, ESamuraiDashRecoveryCondition::AllowDash)) return false;
			int32 Selectors = 0;
			for (const auto& Step : Child.ChildComposite->Children)
			{
				if (auto* Select = Cast<UBTT_SelectBossDestinationPoint>(Step.ChildTask))
				{
					if (!Cast<UBTT_SelectSamuraiDashPoint>(Select)) return false;
					if (Select->GetSelectedBlackboardKey() != TEXT("DestinationLocation")) return false;
					++Selectors;
				}
				if (auto* Activate = Cast<UBTT_ActivateBossAbility>(Step.ChildTask))
				{
					if (Tag == GameplayAbility_Boss_DashSlash && !Activate->RequiresPreselectedDestination()) return false;
					if (Tag == GameplayAbility_Boss_ChainSlashDash && (Activate->RequiresPreselectedDestination() || Activate->PrefersCurrentWeaponAbility())) return false;
				}
			}
			if (Tag == GameplayAbility_Boss_DashSlash) { if (Selectors != 1) return false; ++Single; }
			else { if (Selectors != 0) return false; ++Chain; }
		}
		for (UEdGraphNode* Node : Tree->BTGraph->Nodes)
			if (auto* G = Cast<UBehaviorTreeGraphNode>(Node); G && !G->IsA<UBehaviorTreeGraphNode_Root>())
			{
				if (!G->NodeInstance || G->HasErrors()) return false;
				for (const auto& Sub : G->SubNodes) if (!Sub || !Sub->NodeInstance || Sub->HasErrors()) return false;
			}
		UE_LOG(LogTemp, Display, TEXT("Samurai Dash structure: priority reposition, gated normal actions, single=%d, chain=%d, graph valid."), Single, Chain);
		auto* SamuraiClass = LoadClass<AShipBossEnemy>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T4/T4_BP_ShipBoss_Samurai.T4_BP_ShipBoss_Samurai_C"));
		auto* Samurai = SamuraiClass ? SamuraiClass->GetDefaultObject<AShipBossEnemy>() : nullptr;
		auto* Set = Samurai ? Samurai->GetBehaviorSet() : nullptr;
		auto* Policy = Samurai ? Samurai->GetSamuraiDashRecoveryComponent() : nullptr;
		if (!Set || Set->FindSubtree(EEnemyAIState::Combat) != Tree || !Policy
			|| Policy->SamuraiCombatTree.ToSoftObjectPath() != FSoftObjectPath(Tree)) return false;
		UE_LOG(LogTemp, Display, TEXT("Samurai BP routing matches recovery scope. Blackboard=%s MaxFailures=%d Retry=%.2fs MaxSelection=%.2fs"),
			*Tree->BlackboardAsset->GetName(), Policy->MaximumPointFailures, Policy->SelectionRetryInterval, Policy->MaximumSelectionTime);
		return Single == 1 && Chain == 1;
	}
}
#endif

USamuraiDashAuthoringCommandlet::USamuraiDashAuthoringCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 USamuraiDashAuthoringCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	auto* Tree = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/SamuraiBoss/BT_Subtree_SamuraiBoss_Combat.BT_Subtree_SamuraiBoss_Combat"));
	if (!Tree || !Tree->RootNode) return 1;
	if (FParse::Param(*Params, TEXT("AuditOnly"))) return Audit(Tree) ? 0 : 1;
	// Idempotent migration: preserve subsequent editor adjustments.
	if (Tree->RootNode->Children.Num() == 2 && Cast<UBTT_RepositionSamuraiAfterDashFailure>(Tree->RootNode->Children[0].ChildTask))
	{
		if (FParse::Param(*Params, TEXT("RepairBindings")))
		{
			UBTCompositeNode* Attack = Tree->RootNode->Children[1].ChildComposite;
			if (!Attack) return 1;
			for (auto& Child : Attack->Children)
			{
				const auto Tag = AbilityTag(Child.ChildComposite);
				if (Tag != GameplayAbility_Boss_DashSlash && Tag != GameplayAbility_Boss_ChainSlashDash
					&& !HasGate(Child, ESamuraiDashRecoveryCondition::AllowNormalBehavior))
				{
					Gate(Tree, Child, ESamuraiDashRecoveryCondition::AllowNormalBehavior);
				}
			}
			return RebuildEditorGraph(Tree) && Audit(Tree) && SaveTree(Tree) ? 0 : 1;
		}
		return Audit(Tree) ? 0 : 1;
	}
	if (!Cast<UBTComposite_Selector>(Tree->RootNode) || Tree->RootNode->Children.Num() != 1) return 1;
	UBTCompositeNode* Attack = Tree->RootNode->Children[0].ChildComposite;
	if (!Attack || Attack->Children.Num() != 5) return 1;
	int32 Single = 0, Chain = 0;
	for (auto& Child : Attack->Children)
	{
		const FGameplayTag Tag = AbilityTag(Child.ChildComposite);
		if (Tag != GameplayAbility_Boss_DashSlash && Tag != GameplayAbility_Boss_ChainSlashDash)
		{
			Gate(Tree, Child, ESamuraiDashRecoveryCondition::AllowNormalBehavior);
			continue;
		}
		if (Tag == GameplayAbility_Boss_DashSlash) ++Single; else ++Chain;
		Gate(Tree, Child, ESamuraiDashRecoveryCondition::AllowDash);
		for (auto& D : Child.Decorators)
			if (D->IsA<UBTD_CanActivateAbilityByTag>())
			{
				// Conditions select a new attack; they must not preempt a committed reposition.
				auto* P = FindFProperty<FByteProperty>(D->GetClass(), TEXT("FlowAbortMode"));
				check(P); P->SetPropertyValue_InContainer(D, EBTFlowAbortMode::None);
			}
		for (int32 Index = Child.ChildComposite->Children.Num() - 1; Index >= 0; --Index)
		{
			auto& Step = Child.ChildComposite->Children[Index];
			if (auto* Old = Cast<UBTT_SelectBossDestinationPoint>(Step.ChildTask))
			{
				if (Tag == GameplayAbility_Boss_ChainSlashDash) { Child.ChildComposite->Children.RemoveAt(Index); continue; }
				auto* Replacement = NewObject<UBTT_SelectSamuraiDashPoint>(Tree, NAME_None, RF_Transactional);
				for (const FName Name : { FName(TEXT("BlackboardKey")), FName(TEXT("TargetActorKey")), FName(TEXT("DestinationRelation")), FName(TEXT("SelectionSettings")) })
				{
					auto* P = Old->GetClass()->FindPropertyByName(Name); check(P);
					P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(Replacement), P->ContainerPtrToValuePtr<void>(Old));
				}
				Step.ChildTask = Replacement;
			}
			if (auto* Activate = Cast<UBTT_ActivateBossAbility>(Step.ChildTask))
			{
				SetBool(Activate, TEXT("bRequirePreselectedDestination"), Tag == GameplayAbility_Boss_DashSlash);
				SetBool(Activate, TEXT("bPreferCurrentWeaponAbility"), false);
				SetBool(Activate, TEXT("bClearDestinationWhenFinished"), true);
			}
		}
	}
	if (Single != 1 || Chain != 1) return 1;
	Gate(Tree, Tree->RootNode->Children[0], ESamuraiDashRecoveryCondition::AllowNormalBehavior);
	FBTCompositeChild Reposition;
	Reposition.ChildTask = NewObject<UBTT_RepositionSamuraiAfterDashFailure>(Tree, NAME_None, RF_Transactional);
	Gate(Tree, Reposition, ESamuraiDashRecoveryCondition::RepositionReady);
	Tree->RootNode->Children.Insert(Reposition, 0);
	if (!RebuildEditorGraph(Tree) || !Audit(Tree) || !SaveTree(Tree)) return 1;
	UE_LOG(LogTemp, Display, TEXT("Saved Samurai Combat Dash migration. Other Behavior Trees/Blueprints unchanged."));
	return 0;
#else
	return 1;
#endif
}
