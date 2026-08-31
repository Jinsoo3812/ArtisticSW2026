#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DataValidation.h"
#include "Ship.h"
#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"
#include "ShipAI/BTT_ActivateEnemyShipAbility.h"
#include "ShipAI/BTT_EnableEnemyShipNavigation.h"
#include "ShipAI/BTT_SelectEnemyShipAbility.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "ShipAI/NavalAIController.h"

namespace EnemyShipCannonOnlyAuthoringTests
{
	const FBlackboardEntry* FindKey(const UBlackboardData* Blackboard, const FName KeyName)
	{
		if (!Blackboard)
		{
			return nullptr;
		}
		return Blackboard->GetKeys().FindByPredicate([KeyName](const FBlackboardEntry& Entry)
		{
			return Entry.EntryName == KeyName;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipCannonOnlyAuthoringTest,
	"ArtisticSW.Enemy.Ship.Authoring.CannonOnlyAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipCannonOnlyAuthoringTest::RunTest(const FString& Parameters)
{
	using namespace EnemyShipCannonOnlyAuthoringTests;

	UClass* ShipClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C"));
	AEnemyShip* ShipCDO = ShipClass ? Cast<AEnemyShip>(ShipClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("BP_EnemyShip generated class loads"), ShipClass)
		|| !TestNotNull(TEXT("BP_EnemyShip derives from AEnemyShip"), ShipCDO))
	{
		return false;
	}
	const FProperty* ArchetypeProperty = FindFProperty<FProperty>(
		AEnemyShip::StaticClass(), GET_MEMBER_NAME_CHECKED(AEnemyShip, EnemyShipArchetype));
	TestNotNull(TEXT("EnemyShipArchetype property exists"), ArchetypeProperty);
	TestTrue(
		TEXT("EnemyShipArchetype can be overridden on placed instances"),
		ArchetypeProperty && !ArchetypeProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance));

	UEnemyShipArchetypeData* Archetype = LoadObject<UEnemyShipArchetypeData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/DA_ES_Archetype_Cannon.DA_ES_Archetype_Cannon"));
	UEnemyShipPatternData* Pattern = LoadObject<UEnemyShipPatternData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Pattern/DA_ES_Pattern_Cannon.DA_ES_Pattern_Cannon"));
	UEnemyShipSkillModuleData* Module = LoadObject<UEnemyShipSkillModuleData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Cannon.DA_ES_SkillModule_Cannon"));
	UEnemyShipAbilitySet* AbilitySet = LoadObject<UEnemyShipAbilitySet>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/AbilitySet/DA_ES_AbilitySet_Cannon.DA_ES_AbilitySet_Cannon"));

	if (!TestNotNull(TEXT("Cannon Archetype loads"), Archetype)
		|| !TestNotNull(TEXT("CannonVolley Pattern loads"), Pattern)
		|| !TestNotNull(TEXT("CannonVolley Skill Module loads"), Module)
		|| !TestNotNull(TEXT("CannonVolley Ability Set loads"), AbilitySet))
	{
		return false;
	}

	TestTrue(TEXT("BP_EnemyShip uses Cannon Archetype"), ShipCDO->EnemyShipArchetype == Archetype);
	TestTrue(TEXT("Core Skill Modules are intentionally empty"), ShipCDO->CoreSkillModules.IsEmpty());
	TestTrue(
		TEXT("BP_EnemyShip uses a Naval AI Controller"),
		ShipCDO->AIControllerClass && ShipCDO->AIControllerClass->IsChildOf(ANavalAIController::StaticClass()));
	TestEqual(
		TEXT("BP_EnemyShip auto-possesses when placed or spawned"),
		ShipCDO->AutoPossessAI,
		EAutoPossessAI::PlacedInWorldOrSpawned);
	TestTrue(
		TEXT("Navigation Component replicates authoritative debug state to PIE clients"),
		ShipCDO->GetNavigationComponent() && ShipCDO->GetNavigationComponent()->GetIsReplicated());

	UBlueprint* ShipBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip"));
	TestNotNull(TEXT("BP_EnemyShip Blueprint asset loads"), ShipBlueprint);
	int32 CannonChildActorCount = 0;
	const TArray<USCS_Node*> ConstructionNodes = ShipBlueprint && ShipBlueprint->SimpleConstructionScript
		? ShipBlueprint->SimpleConstructionScript->GetAllNodes()
		: TArray<USCS_Node*>();
	for (const USCS_Node* Node : ConstructionNodes)
	{
		const UChildActorComponent* ChildActorTemplate = Node
			? Cast<UChildActorComponent>(Node->ComponentTemplate)
			: nullptr;
		if (ChildActorTemplate && ChildActorTemplate->GetChildActorClass()
			&& ChildActorTemplate->GetChildActorClass()->IsChildOf(ACannon::StaticClass()))
		{
			++CannonChildActorCount;
		}
	}
	TestTrue(TEXT("BP_EnemyShip has at least one mounted Cannon child actor"), CannonChildActorCount > 0);

	TestTrue(TEXT("Archetype points to CannonVolley Pattern"), Archetype->Pattern == Pattern);
	TestNotNull(TEXT("Archetype Spec Row has a Data Table"), Archetype->SpecRow.DataTable.Get());
	TestEqual(TEXT("Archetype uses EnemyShip_Normal Spec Row"), Archetype->SpecRow.RowName, FName(TEXT("EnemyShip_Normal")));
	TestEqual(TEXT("CannonVolley Pattern has one explicit Skill Module"), Pattern->SkillModules.Num(), 1);
	TestTrue(
		TEXT("Pattern explicitly contains CannonVolley Module"),
		Pattern->SkillModules.Num() == 1 && Pattern->SkillModules[0] == Module);

	TestEqual(TEXT("CannonVolley Module ID is stable"), Module->ModuleId, FName(TEXT("Skill.CannonVolley")));
	TestTrue(TEXT("CannonVolley Module points to its Ability Set"), Module->AbilitySet == AbilitySet);
	TestEqual(TEXT("CannonVolley Module has one Skill Rule"), Module->SkillRules.Num(), 1);
	if (Module->SkillRules.Num() == 1)
	{
		const FEnemyShipSkillRule& Rule = Module->SkillRules[0];
		TestEqual(TEXT("Cannon Rule ID is stable"), Rule.RuleId, FName(TEXT("Skill.Cannon.Use")));
		TestTrue(
			TEXT("CannonVolley Rule uses the correct Ability Tag"),
			Rule.AbilityTag == FGameplayTag(GameplayAbility_EnemyShip_CannonVolley));
		TestTrue(
			TEXT("CannonVolley Rule Ability Class derives from the Native GA"),
			Rule.AbilityClass && Rule.AbilityClass->IsChildOf(UGA_EnemyShipCannonVolley::StaticClass()));
		TestEqual(
			TEXT("CannonVolley keeps normal navigation active"),
			Rule.MovementPolicy,
			EEnemyShipSkillMovementPolicy::ContinueNavigation);
		TestTrue(TEXT("CannonVolley Rule allows Orbit"), Rule.AllowedNavigationStates.Contains(ENavalCombatState::Orbit));
		TestTrue(TEXT("CannonVolley Rule allows danger-close Retreat"), Rule.AllowedNavigationStates.Contains(ENavalCombatState::Retreat));
	}

	TestEqual(TEXT("CannonVolley Ability Set grants one Ability"), AbilitySet->Abilities.Num(), 1);
	TestTrue(
		TEXT("Granted CannonVolley Ability derives from the Native GA"),
		AbilitySet->Abilities.Num() == 1 && AbilitySet->Abilities[0]
			&& AbilitySet->Abilities[0]->IsChildOf(UGA_EnemyShipCannonVolley::StaticClass()));

	FDataValidationContext ValidationContext;
	TestFalse(
		TEXT("CannonVolley Module passes Data Validation"),
		Module->IsDataValid(ValidationContext) == EDataValidationResult::Invalid);
	TestFalse(
		TEXT("CannonVolley Pattern passes Data Validation"),
		Pattern->IsDataValid(ValidationContext) == EDataValidationResult::Invalid);
	TestFalse(
		TEXT("Cannon Archetype passes Data Validation"),
		Archetype->IsDataValid(ValidationContext) == EDataValidationResult::Invalid);

	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/AI/BB_NavalAI.BB_NavalAI"));
	if (!TestNotNull(TEXT("BB_NavalAI loads"), Blackboard))
	{
		return false;
	}
	const FBlackboardEntry* TargetKey = FindKey(Blackboard, TEXT("TargetShip"));
	const FBlackboardEntry* AbilityTagKey = FindKey(Blackboard, TEXT("SelectedAbilityTag"));
	const FBlackboardEntry* RuleIdKey = FindKey(Blackboard, TEXT("SelectedAbilityRuleId"));
	TestNotNull(TEXT("Blackboard has TargetShip"), TargetKey);
	TestNotNull(TEXT("Blackboard has SelectedAbilityTag"), AbilityTagKey);
	TestNotNull(TEXT("Blackboard has SelectedAbilityRuleId"), RuleIdKey);
	TestTrue(
		TEXT("TargetShip is an Object key"),
		TargetKey && TargetKey->KeyType && TargetKey->KeyType->IsA<UBlackboardKeyType_Object>());
	TestTrue(
		TEXT("SelectedAbilityTag is a Name key"),
		AbilityTagKey && AbilityTagKey->KeyType && AbilityTagKey->KeyType->IsA<UBlackboardKeyType_Name>());
	TestTrue(
		TEXT("SelectedAbilityRuleId is a Name key"),
		RuleIdKey && RuleIdKey->KeyType && RuleIdKey->KeyType->IsA<UBlackboardKeyType_Name>());
	TestNull(TEXT("Legacy IdealDistance Blackboard key is removed"), FindKey(Blackboard, TEXT("IdealDistance")));
	TestNull(TEXT("Legacy ReturnPoint Blackboard key is removed"), FindKey(Blackboard, TEXT("ReturnPoint")));
	TestNull(TEXT("Legacy ReturnArrivalOffset Blackboard key is removed"), FindKey(Blackboard, TEXT("ReturnArrivalOffset")));

	UBehaviorTree* BehaviorTree = LoadObject<UBehaviorTree>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/AI/BT_NavalAI.BT_NavalAI"));
	if (!TestNotNull(TEXT("BT_NavalAI loads"), BehaviorTree))
	{
		return false;
	}
	TestTrue(TEXT("BT_NavalAI uses BB_NavalAI"), BehaviorTree->BlackboardAsset == Blackboard);
	UBTComposite_Sequence* RootSequence = Cast<UBTComposite_Sequence>(BehaviorTree->RootNode);
	TestNotNull(TEXT("Behavior Tree root child is a Sequence"), RootSequence);
	if (RootSequence)
	{
		TestEqual(TEXT("Root Sequence has Navigation and Selector children"), RootSequence->GetChildrenNum(), 2);
		const UBTT_EnableEnemyShipNavigation* EnableNavigationTask = RootSequence->GetChildrenNum() > 0
			? Cast<UBTT_EnableEnemyShipNavigation>(RootSequence->GetChildNode(0))
			: nullptr;
		TestTrue(
			TEXT("Root Sequence first child enables Enemy Ship navigation"),
			EnableNavigationTask && EnableNavigationTask->GetEnableNavigation());
		UBTComposite_Selector* Selector = RootSequence->GetChildrenNum() > 1
			? Cast<UBTComposite_Selector>(RootSequence->GetChildNode(1))
			: nullptr;
		TestNotNull(TEXT("Root Sequence second child is the skill Selector"), Selector);
		if (Selector)
		{
			TestEqual(TEXT("Skill Selector has skill Sequence and Wait fallback"), Selector->GetChildrenNum(), 2);
			UBTComposite_Sequence* SkillSequence = Selector->GetChildrenNum() > 0
				? Cast<UBTComposite_Sequence>(Selector->GetChildNode(0))
				: nullptr;
			TestNotNull(TEXT("Selector first child is the skill Sequence"), SkillSequence);
			TestTrue(
				TEXT("Selector second child is Wait fallback"),
				Selector->GetChildrenNum() > 1 && Selector->GetChildNode(1)->IsA<UBTTask_Wait>());
			if (SkillSequence)
			{
				TestEqual(TEXT("Skill Sequence has Select and Activate tasks"), SkillSequence->GetChildrenNum(), 2);
				const UBTT_SelectEnemyShipAbility* SelectTask = SkillSequence->GetChildrenNum() > 0
					? Cast<UBTT_SelectEnemyShipAbility>(SkillSequence->GetChildNode(0))
					: nullptr;
				const UBTT_ActivateEnemyShipAbility* ActivateTask = SkillSequence->GetChildrenNum() > 1
					? Cast<UBTT_ActivateEnemyShipAbility>(SkillSequence->GetChildNode(1))
					: nullptr;
				TestTrue(
					TEXT("Skill Sequence first child selects an Ability"),
					SelectTask != nullptr);
				TestTrue(
					TEXT("Skill Sequence second child activates the selection"),
					ActivateTask != nullptr);
				if (SelectTask)
				{
					TestEqual(TEXT("Select Task reads TargetShip"), SelectTask->GetTargetShipKeyName(), FName(TEXT("TargetShip")));
					TestEqual(TEXT("Select Task writes SelectedAbilityTag"), SelectTask->GetSelectedAbilityTagKeyName(), FName(TEXT("SelectedAbilityTag")));
					TestEqual(TEXT("Select Task writes SelectedAbilityRuleId"), SelectTask->GetSelectedRuleIdKeyName(), FName(TEXT("SelectedAbilityRuleId")));
				}
				if (ActivateTask)
				{
					TestEqual(TEXT("Activate Task reads SelectedAbilityTag"), ActivateTask->GetSelectedAbilityTagKeyName(), FName(TEXT("SelectedAbilityTag")));
					TestEqual(TEXT("Activate Task reads SelectedAbilityRuleId"), ActivateTask->GetSelectedRuleIdKeyName(), FName(TEXT("SelectedAbilityRuleId")));
					TestTrue(TEXT("Activate Task cancels an Ability when BT aborts"), ActivateTask->GetCancelAbilityOnAbort());
				}
			}
		}
	}

	TestNotNull(
		TEXT("p.ShowEnemyShipAIDebug console variable is registered"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("p.ShowEnemyShipAIDebug")));
	TestNotNull(
		TEXT("p.EnemyShipAIDebugHeight console variable is registered"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("p.EnemyShipAIDebugHeight")));
	return true;
}

#endif
