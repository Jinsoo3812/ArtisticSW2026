#include "Task/BTT_SelectBossDestinationPoint.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"

UBTT_SelectBossDestinationPoint::UBTT_SelectBossDestinationPoint()
{
	NodeName = TEXT("Select Boss Destination Point");
	BlackboardKey.SelectedKeyName = TEXT("DestinationPointId");
	BlackboardKey.AddIntFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_SelectBossDestinationPoint, BlackboardKey));
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	TargetActorKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_SelectBossDestinationPoint, TargetActorKey),
		AActor::StaticClass());
}

EBTNodeResult::Type UBTT_SelectBossDestinationPoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	AShipBossEnemy* Boss = Controller ? Cast<AShipBossEnemy>(Controller->GetPawn()) : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName))
		: nullptr;
	if (!Target && Boss)
	{
		Target = Boss->GetBossCombatTarget();
	}
	if (!Boss || !Blackboard || !Boss->CanEngageActor(Target))
	{
		return EBTNodeResult::Failed;
	}

	int32 PointId = INDEX_NONE;
	if (!UBossDeckPointSelector::SelectDestinationPoint(
		Boss->GetHostShip(), Boss, Target, SelectionPurpose, DestinationRelation, SelectionSettings, PointId))
	{
		Blackboard->SetValueAsInt(GetSelectedBlackboardKey(), INDEX_NONE);
		Boss->SetDestinationPointId(INDEX_NONE);
		return EBTNodeResult::Failed;
	}

	if (!Boss->TrySetDestinationPointId(PointId))
	{
		Blackboard->SetValueAsInt(GetSelectedBlackboardKey(), INDEX_NONE);
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsInt(GetSelectedBlackboardKey(), PointId);
	return EBTNodeResult::Succeeded;
}

FString UBTT_SelectBossDestinationPoint::GetStaticDescription() const
{
	const TCHAR* RelationDescription = SelectionPurpose == EBossDestinationPurpose::Dash
		? TEXT("PathThroughTarget")
		: (DestinationRelation == EBossDestinationRelation::BehindTarget
			? TEXT("Behind")
			: (DestinationRelation == EBossDestinationRelation::InFrontOfTarget
				? TEXT("Front")
				: TEXT("Any")));
	return FString::Printf(
		TEXT("Select moving-deck destination (%s, %s) -> %s"),
		SelectionPurpose == EBossDestinationPurpose::Dash
			? TEXT("Dash")
			: (SelectionPurpose == EBossDestinationPurpose::Walk ? TEXT("Walk") : TEXT("Vanish")),
		RelationDescription,
		*GetSelectedBlackboardKey().ToString());
}
