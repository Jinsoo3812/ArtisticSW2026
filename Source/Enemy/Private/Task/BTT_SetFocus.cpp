#include "Task/BTT_SetFocus.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"

UBTT_SetFocus::UBTT_SetFocus()
{
	NodeName = TEXT("Set Focus");

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_SetFocus, BlackboardKey),
		AActor::StaticClass());
}

EBTNodeResult::Type UBTT_SetFocus::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	if (!AIController || !BlackboardComponent)
	{
		return EBTNodeResult::Failed;
	}

	AActor* TargetActor = Cast<AActor>(
		BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()));
	if (!IsValid(TargetActor))
	{
		return EBTNodeResult::Failed;
	}

	AIController->SetFocus(TargetActor, EAIFocusPriority::Gameplay);
	return EBTNodeResult::Succeeded;
}

FString UBTT_SetFocus::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Set Gameplay focus to: %s"),
		*BlackboardKey.SelectedKeyName.ToString());
}
