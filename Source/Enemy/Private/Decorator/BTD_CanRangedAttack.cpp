#include "Decorator/BTD_CanRangedAttack.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "RangedEnemy/RangedEnemy.h"

UBTD_CanRangedAttack::UBTD_CanRangedAttack()
{
	NodeName = TEXT("Can Ranged Attack");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_CanRangedAttack, BlackboardKey),
		AActor::StaticClass());
}

bool UBTD_CanRangedAttack::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const ARangedEnemy* Enemy = AIController ? Cast<ARangedEnemy>(AIController->GetPawn()) : nullptr;
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	const AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;

	return Enemy && Enemy->CanAttackTarget(TargetActor, bRequireLineOfSight);
}

