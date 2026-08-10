#include "ShipAI/BTT_SelectEnemyShipAbility.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

UBTT_SelectEnemyShipAbility::UBTT_SelectEnemyShipAbility()
{
	NodeName = TEXT("Select Enemy Ship Ability");
	TargetShipKey.SelectedKeyName = TEXT("TargetShip");
	TargetShipKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_SelectEnemyShipAbility, TargetShipKey), AActor::StaticClass());
	SelectedAbilityTagKey.SelectedKeyName = TEXT("SelectedAbilityTag");
	SelectedAbilityTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_SelectEnemyShipAbility, SelectedAbilityTagKey));
	SelectedRuleIndexKey.SelectedKeyName = TEXT("SelectedAbilityRuleIndex");
	SelectedRuleIndexKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_SelectEnemyShipAbility, SelectedRuleIndexKey));
}

EBTNodeResult::Type UBTT_SelectEnemyShipAbility::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AEnemyShip* Ship = Controller ? Cast<AEnemyShip>(Controller->GetPawn()) : nullptr;
	UEnemyShipPatternRuntimeComponent* Runtime = Ship ? Ship->GetPatternRuntimeComponent() : nullptr;
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(TargetShipKey.SelectedKeyName))
		: nullptr;
	if (!Blackboard || !Runtime || !Target)
	{
		return EBTNodeResult::Failed;
	}

	FEnemyShipAbilitySelection Selection;
	if (!Runtime->SelectAbility(Target, Selection))
	{
		Blackboard->ClearValue(SelectedAbilityTagKey.SelectedKeyName);
		Blackboard->SetValueAsInt(SelectedRuleIndexKey.SelectedKeyName, INDEX_NONE);
		return EBTNodeResult::Failed;
	}

	Blackboard->SetValueAsName(SelectedAbilityTagKey.SelectedKeyName, Selection.AbilityTag.GetTagName());
	Blackboard->SetValueAsInt(SelectedRuleIndexKey.SelectedKeyName, Selection.RuleIndex);
	return EBTNodeResult::Succeeded;
}
