#include "Decorator/BTD_HasDeckReleaseLOSReposition.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"

UBTD_HasDeckReleaseLOSReposition::UBTD_HasDeckReleaseLOSReposition()
{
	NodeName = TEXT("Has Deck LOS Recovery");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_HasDeckReleaseLOSReposition, BlackboardKey),
		AActor::StaticClass());
}

bool UBTD_HasDeckReleaseLOSReposition::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const ADeckEnemy* Enemy = Controller ? Cast<ADeckEnemy>(Controller->GetPawn()) : nullptr;
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* TargetActor = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	const UDeckEnemyCombatComponent* Combat = Enemy
		? Enemy->GetDeckCombatComponent()
		: nullptr;

	return Combat && Combat->HasRecovery(TargetActor);
}
