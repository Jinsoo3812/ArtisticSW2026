#include "Decorator/BTD_CombatTargetState.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTD_CombatTargetState::UBTD_CombatTargetState()
{
	NodeName = TEXT("Combat Target State");
	FlowAbortMode = EBTFlowAbortMode::Both;
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_CombatTargetState, BlackboardKey),
		AActor::StaticClass());
}

bool UBTD_CombatTargetState::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	const AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	const bool bTargetIsSet = IsValid(TargetActor);
	return Query == ECombatTargetStateQuery::IsSet ? bTargetIsSet : !bTargetIsSet;
}

FString UBTD_CombatTargetState::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s: %s"),
		*GetSelectedBlackboardKey().ToString(),
		Query == ECombatTargetStateQuery::IsSet ? TEXT("Is Set") : TEXT("Is Not Set"));
}
