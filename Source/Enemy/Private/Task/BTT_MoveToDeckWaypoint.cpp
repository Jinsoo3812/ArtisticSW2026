#include "Task/BTT_MoveToDeckWaypoint.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

namespace
{
	void StopMovement(UBehaviorTreeComponent& OwnerComp, ACharacter* Character)
	{
		if (AAIController* Controller = OwnerComp.GetAIOwner()) Controller->StopMovement();
		if (Character && Character->GetCharacterMovement()) Character->GetCharacterMovement()->StopMovementImmediately();
	}
	void ClearDestinationKey(UBehaviorTreeComponent& OwnerComp)
	{
		if (UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
			Blackboard && Blackboard->GetKeyID(TEXT("DestinationLocation")) != FBlackboard::InvalidKey)
			Blackboard->ClearValue(TEXT("DestinationLocation"));
	}
}
UBTT_MoveToDeckWaypoint::UBTT_MoveToDeckWaypoint() { NodeName = TEXT("Move On Deck Walk Area"); bNotifyTick = true; }
uint16 UBTT_MoveToDeckWaypoint::GetInstanceMemorySize() const { return 0; }
EBTNodeResult::Type UBTT_MoveToDeckWaypoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = Controller ? Cast<ABaseEnemy>(Controller->GetPawn()) : nullptr;
	IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy);
	AEnemyShip* Ship = Mover ? Mover->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr;
	ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(Enemy);
	if (DeckEnemy && DeckEnemy->CanAttackCurrentTarget(true))
	{
		StopMovement(OwnerComp, Enemy);
		if (DeckEnemy->GetDeckEnemyNavigationComponent()) DeckEnemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
		if (Route) Route->ClearGoal();
		ClearDestinationKey(OwnerComp);
		return EBTNodeResult::Succeeded;
	}
	if (!Mover || !Mover->CanMoveOnDeck() || !Area || !Area->IsReady() || !Route || !Route->HasGoal()
		|| !Enemy->GetCharacterMovement() || !Enemy->GetCharacterMovement()->IsMovingOnGround())
	{
		StopMovement(OwnerComp, Enemy);
		if (Route) Route->ClearGoal();
		if (Mover) Mover->OnDeckMoveFailed();
		ClearDestinationKey(OwnerComp);
		return EBTNodeResult::Failed;
	}
	Enemy->SetBase(Area->GetMovementBase(*Enemy));
	Enemy->SetBaseMovementSpeed(MoveSpeed);
	Enemy->GetCharacterMovement()->BrakingDecelerationWalking = BrakingDeceleration;
	return EBTNodeResult::InProgress;
}
void UBTT_MoveToDeckWaypoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = Controller ? Cast<ABaseEnemy>(Controller->GetPawn()) : nullptr;
	IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy);
	AEnemyShip* Ship = Mover ? Mover->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr;
	ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(Enemy);
	UDeckEnemyNavigationComponent* Navigation = DeckEnemy ? DeckEnemy->GetDeckEnemyNavigationComponent() : nullptr;
	const bool bCombatGoal = (Route && Route->IsTrackingActor())
		|| (Navigation && (Navigation->HasActiveRoute() || Navigation->HasReleaseLineOfSightReposition()));
	bool bAttackReady = false;
	if (DeckEnemy && DeckEnemy->GetCombatTarget())
	{
		FDeckWalkLocation TargetFloor;
		if (!Area || !Area->ResolveActorOnDeck(*DeckEnemy->GetCombatTarget(), TargetFloor))
		{
			DeckEnemy->ClearCombatTarget();
			if (bCombatGoal && Navigation) Navigation->CancelCombatRoute();
		}
		else bAttackReady = DeckEnemy->CanAttackCurrentTarget(true);
	}
	if (Navigation && !bAttackReady) Navigation->ReplanIfTargetMoved(DeckEnemy->GetCombatTarget(), true);
	const EDeckWalkRouteTick Result = bAttackReady ? EDeckWalkRouteTick::Reached
		: (Mover && Mover->CanMoveOnDeck() && Route ? Route->TickRoute(DeltaSeconds, AcceptanceRadius,
			ProgressTimeout, MaximumMoveTime, MoveSpeed, MinimumProgressDistance) : EDeckWalkRouteTick::Failed);
	if (Result == EDeckWalkRouteTick::Moving) return;
	StopMovement(OwnerComp, Enemy);
	if (Result == EDeckWalkRouteTick::Reached && Mover) Mover->OnDeckMoveReached();
	else if (Mover) Mover->OnDeckMoveFailed();
	if (Route) Route->ClearGoal();
	if (bAttackReady && Navigation) Navigation->CancelCombatRoute();
	ClearDestinationKey(OwnerComp);
	FinishLatentTask(OwnerComp, Result == EDeckWalkRouteTick::Reached ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
}
EBTNodeResult::Type UBTT_MoveToDeckWaypoint::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	StopMovement(OwnerComp, Character);
	if (Character)
	{
		if (UDeckWalkRouteComponent* Route = Character->FindComponentByClass<UDeckWalkRouteComponent>()) Route->ClearGoal();
		if (IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Character)) Mover->OnDeckMoveFailed();
	}
	ClearDestinationKey(OwnerComp);
	return EBTNodeResult::Aborted;
}
