#include "Task/BTT_SelectBossDestinationPoint.h"

#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "AI/BaseAIController.h"

UBTT_SelectBossDestinationPoint::UBTT_SelectBossDestinationPoint()
{
	NodeName = TEXT("Select Boss Walk Area Destination");
	bNotifyTick = true;
	BlackboardKey.SelectedKeyName = TEXT("DestinationLocation");
	BlackboardKey.AddVectorFilter(
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
	// A committed ability may outlive its BT branch (e.g. its player dies).
	// Failed selectors must not clear or replace that ability's reserved endpoint.
	if (Boss && Boss->GetAbilitySystemComponent()
		&& Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy)) return EBTNodeResult::Failed;
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

	FDeckWalkLocation Location;
	if (!UBossDeckPointSelector::SelectDestinationLocation(
		Boss->GetHostShip(), Boss, Target, SelectionPurpose, DestinationRelation, SelectionSettings, Location))
	{
		const auto* Resolver = Boss->FindComponentByClass<UDeckCombatTargetResolverComponent>();
		if (Resolver && Resolver->HasExpiredEvidence(Target))
		{
			Boss->ClearDestination(); Blackboard->ClearValue(GetSelectedBlackboardKey());
			Boss->SetBossCombatTarget(nullptr);
			if (auto* AI = Cast<ABaseAIController>(Controller)) AI->ClearCombatTarget(true);
			return EBTNodeResult::Failed;
		}
		if (SelectionPurpose == EBossDestinationPurpose::Walk && Boss->HasDestination()
			&& Boss->GetDeckWalkRouteComponent()->HasGoal())
		{
			Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Boss->GetDestinationLocation().LocalFloor);
			return EBTNodeResult::Succeeded;
		}
		if (!Boss->HasDestination()) { Blackboard->ClearValue(GetSelectedBlackboardKey()); Boss->ClearDestination(); }
		*reinterpret_cast<float*>(NodeMemory) = 0.f;
		return EBTNodeResult::InProgress;
	}

	if (!Boss->TrySetDestinationLocation(Location, SelectionPurpose == EBossDestinationPurpose::Walk))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Selected boss destination was rejected."));
		if (!Boss->HasDestination()) { Blackboard->ClearValue(GetSelectedBlackboardKey()); Boss->ClearDestination(); }
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Location.LocalFloor);
	if (SelectionPurpose == EBossDestinationPurpose::Walk) Boss->TrackWalkingTarget(Target, SelectionSettings);
	return EBTNodeResult::Succeeded;
}

void UBTT_SelectBossDestinationPoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	float& Waiting = *reinterpret_cast<float*>(NodeMemory); Waiting += DeltaSeconds;
	if (Waiting >= 0.3f) FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
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
