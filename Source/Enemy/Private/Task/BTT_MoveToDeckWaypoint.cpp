#include "Task/BTT_MoveToDeckWaypoint.h"
#include "AI/BaseAIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

namespace
{
	void StopDeckMove(UBehaviorTreeComponent& OwnerComp, ABaseEnemy* Enemy, bool bReached)
	{
		if (AAIController* AI = OwnerComp.GetAIOwner()) AI->StopMovement();
		if (Enemy && Enemy->GetCharacterMovement()) Enemy->GetCharacterMovement()->StopMovementImmediately();
		if (IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy))
		{
			if (bReached) Mover->OnDeckMoveReached(); else Mover->OnDeckMoveFailed();
		}
		if (UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr) Route->ClearGoal();
		if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent(); BB && BB->GetKeyID(TEXT("DestinationLocation")) != FBlackboard::InvalidKey)
			BB->ClearValue(TEXT("DestinationLocation"));
	}
	bool AttackReady(UBehaviorTreeComponent& OwnerComp, ADeckEnemy* Enemy)
	{
		const ABaseAIController* AI = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
		return AI && AI->GetEnemyState() == EEnemyAIState::Combat && Enemy && Enemy->GetDeckCombatComponent()
			&& Enemy->GetDeckCombatComponent()->EvaluateAttack(AI->GetCombatTarget()) == EDeckAttackOutcome::Ready;
	}
}
UBTT_MoveToDeckWaypoint::UBTT_MoveToDeckWaypoint() { NodeName = TEXT("Move On Deck Walk Area"); bNotifyTick = true; }
uint16 UBTT_MoveToDeckWaypoint::GetInstanceMemorySize() const { return 0; }
EBTNodeResult::Type UBTT_MoveToDeckWaypoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AI = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AI ? Cast<ABaseEnemy>(AI->GetPawn()) : nullptr;
	IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy);
	AEnemyShip* Ship = Mover ? Mover->GetDeckHostShip() : nullptr;
	UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr;
	if (bStopWhenAttackReady && AttackReady(OwnerComp, Cast<ADeckEnemy>(Enemy)))
	{
		StopDeckMove(OwnerComp, Enemy, true);
		return EBTNodeResult::Succeeded;
	}
	if (!Mover || !Mover->CanMoveOnDeck() || !Area || !Area->IsReady() || !Route || !Route->HasGoal()
		|| !Enemy->GetCharacterMovement() || !Enemy->GetCharacterMovement()->IsMovingOnGround())
	{
		StopDeckMove(OwnerComp, Enemy, false); return EBTNodeResult::Failed;
	}
	Enemy->SetBase(Area->GetMovementBase(*Enemy));
	Enemy->SetBaseMovementSpeed(MoveSpeed);
	Enemy->GetCharacterMovement()->BrakingDecelerationWalking = BrakingDeceleration;
	return EBTNodeResult::InProgress;
}
void UBTT_MoveToDeckWaypoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ABaseEnemy* Enemy = OwnerComp.GetAIOwner() ? Cast<ABaseEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy);
	ADeckEnemy* Deck = Cast<ADeckEnemy>(Enemy);
	UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr;
	const bool bReady = bStopWhenAttackReady && AttackReady(OwnerComp, Deck);
	if (Deck && !bReady) Deck->GetDeckEnemyNavigationComponent()->ReplanIfTargetMoved(Deck->GetCombatTarget());
	const EDeckWalkRouteTick Result = bReady ? EDeckWalkRouteTick::Reached
		: (Mover && Mover->CanMoveOnDeck() && Route ? Route->TickRoute(DeltaSeconds, AcceptanceRadius,
			ProgressTimeout, MaximumMoveTime, MoveSpeed, MinimumProgressDistance) : EDeckWalkRouteTick::Failed);
	if (Result == EDeckWalkRouteTick::Moving) return;
	if (Deck && Deck->GetDeckCombatComponent()->HasRecovery()
		&& Deck->GetDeckCombatComponent()->HasAttackPosition(Deck->GetCombatTarget())) Deck->GetDeckCombatComponent()->ClearRecovery();
	StopDeckMove(OwnerComp, Enemy, Result == EDeckWalkRouteTick::Reached);
	FinishLatentTask(OwnerComp, Result == EDeckWalkRouteTick::Reached ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
}
EBTNodeResult::Type UBTT_MoveToDeckWaypoint::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	StopDeckMove(OwnerComp, OwnerComp.GetAIOwner() ? Cast<ABaseEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr, false);
	return EBTNodeResult::Aborted;
}
