#include "Task/BTT_MoveToDeckWaypoint.h"
#include "AI/BaseAIController.h"
#include "AI/PointSelectionFailure.h"
#include "AI/EnemyAlarmComponent.h"
#include "BaseEnemy.h"
#include "BossAI/ShipBossEnemy.h"
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
	struct FDeckMoveMemory
	{
		FVector InvestigationLocalPoint = FVector::ZeroVector;
		float RetryWaiting = 0.f;
		int32 InvestigationRetries = 0;
		bool bRetryInvestigation = false;
	};
	void StopDeckMove(UBehaviorTreeComponent& OwnerComp, ABaseEnemy* Enemy, bool bReached)
	{
		UDeckWalkRouteComponent* Route = Enemy ? Enemy->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr;
		if (Route) Route->StopWalkingMovement();
		if (IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(Enemy))
		{
			if (bReached) Mover->OnDeckMoveReached(); else Mover->OnDeckMoveFailed();
		}
		if (Route && Route->HasGoal()) Route->ClearGoal();
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
uint16 UBTT_MoveToDeckWaypoint::GetInstanceMemorySize() const { return sizeof(FDeckMoveMemory); }
EBTNodeResult::Type UBTT_MoveToDeckWaypoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	*reinterpret_cast<FDeckMoveMemory*>(NodeMemory) = FDeckMoveMemory();
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
		if (Route && !Route->HasGoal())
			EnemyPointSelectionFailure::Log(this, Enemy, TEXT("Deck move has no valid selected destination point."));
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
	FDeckMoveMemory& Memory = *reinterpret_cast<FDeckMoveMemory*>(NodeMemory);
	if (Memory.bRetryInvestigation && !bReady)
	{
		ABaseAIController* AI = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
		UDeckWalkAreaComponent* Area = Deck && Deck->GetDeckHostShip() ? Deck->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
		if (!Deck || !Deck->CanMoveOnDeck() || !Area || !AI || AI->GetEnemyState() != EEnemyAIState::Investigating)
		{
			StopDeckMove(OwnerComp, Enemy, false); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return;
		}
		Memory.RetryWaiting += DeltaSeconds;
		if (Memory.RetryWaiting > MaximumMoveTime)
		{
			StopDeckMove(OwnerComp, Enemy, false); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return;
		}
		if (Route->IsGoalSelectionDelayed()) return;
		++Memory.InvestigationRetries;
		if (!Deck->GetDeckEnemyNavigationComponent()->PlanInvestigationRoute(Area->ToWorld(Memory.InvestigationLocalPoint)))
		{
			if (Memory.InvestigationRetries >= 3)
			{ StopDeckMove(OwnerComp, Enemy, false); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); }
			return;
		}
		Memory.bRetryInvestigation = false;
	}
	if (Deck && !bReady) Deck->GetDeckEnemyNavigationComponent()->ReplanIfTargetMoved(Deck->GetCombatTarget());
	if (AShipBossEnemy* Boss = Cast<AShipBossEnemy>(Enemy))
	{
		Boss->ReplanWalkingTarget();
		if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent(); BB && Boss->HasDestination()
			&& BB->GetKeyID(TEXT("DestinationLocation")) != FBlackboard::InvalidKey)
			BB->SetValueAsVector(TEXT("DestinationLocation"), Boss->GetDestinationLocation().LocalFloor);
	}
	const EDeckWalkRouteTick Result = bReady ? EDeckWalkRouteTick::Reached
		: (Mover && Mover->CanMoveOnDeck() && Route ? Route->TickRoute(DeltaSeconds, AcceptanceRadius,
			ProgressTimeout, MaximumMoveTime, MoveSpeed, MinimumProgressDistance) : EDeckWalkRouteTick::Failed);
	if (Result == EDeckWalkRouteTick::Moving) return;
	if (Result == EDeckWalkRouteTick::Blocked && Deck && Memory.InvestigationRetries < 3)
	{
		const ABaseAIController* AI = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
		const UEnemyAlarmComponent* Alarm = Deck->FindComponentByClass<UEnemyAlarmComponent>();
		UDeckWalkAreaComponent* Area = Deck->GetDeckHostShip() ? Deck->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
		FVector Point;
		bool bHavePoint = Alarm && Alarm->GetInvestigationWorld(Point);
		if (!bHavePoint)
			if (const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent(); BB && BB->IsVectorValueSet(TEXT("PointOfInterest")))
			{ Point = BB->GetValueAsVector(TEXT("PointOfInterest")); bHavePoint = true; }
		if (AI && AI->GetEnemyState() == EEnemyAIState::Investigating && Area && bHavePoint)
		{
			Memory.InvestigationLocalPoint = Area->ToLocal(Point);
			Memory.RetryWaiting = 0.f; Memory.bRetryInvestigation = true;
			StopDeckMove(OwnerComp, Enemy, false);
			// Preserve the investigation subtree until a real replacement route is installed.
			return;
		}
	}
	if (Result == EDeckWalkRouteTick::Failed || Result == EDeckWalkRouteTick::Blocked)
		EnemyPointSelectionFailure::Log(this, Enemy, TEXT("Deck destination/path could not be retained or replanned."));
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
