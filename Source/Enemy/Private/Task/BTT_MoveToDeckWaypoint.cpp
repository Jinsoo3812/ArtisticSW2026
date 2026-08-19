#include "Task/BTT_MoveToDeckWaypoint.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

UBTT_MoveToDeckWaypoint::UBTT_MoveToDeckWaypoint()
{
	NodeName = TEXT("Move To Live Deck Waypoint");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTT_MoveToDeckWaypoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ADeckRangedEnemy* Enemy = Controller ? Cast<ADeckRangedEnemy>(Controller->GetPawn()) : nullptr;
	AEnemyShip* HostShip = Enemy ? Cast<AEnemyShip>(Enemy->GetHostShip()) : nullptr;
	if (!Enemy || !Enemy->IsPoolActive() || !HostShip
		|| !HostShip->GetDeckWaypoint(Enemy->GetGoalDeckWaypointId()))
	{
		return EBTNodeResult::Failed;
	}

	if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = MoveSpeed;
		Movement->SetMovementMode(MOVE_Walking);
	}
	Enemy->SetBase(HostShip->ShipDeckMesh);
	return EBTNodeResult::InProgress;
}

void UBTT_MoveToDeckWaypoint::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ADeckRangedEnemy* Enemy = Controller ? Cast<ADeckRangedEnemy>(Controller->GetPawn()) : nullptr;
	AEnemyShip* HostShip = Enemy ? Cast<AEnemyShip>(Enemy->GetHostShip()) : nullptr;
	const UDeckWaypointComponent* Goal = HostShip && Enemy
		? HostShip->GetDeckWaypoint(Enemy->GetGoalDeckWaypointId())
		: nullptr;
	if (!Enemy || !Enemy->IsPoolActive() || !HostShip || !HostShip->ShipDeckMesh || !Goal)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FTransform DeckTransform = HostShip->ShipDeckMesh->GetComponentTransform();
	const FVector LocalEnemy = DeckTransform.InverseTransformPosition(Enemy->GetActorLocation());
	const FVector LocalGoal = DeckTransform.InverseTransformPosition(Goal->GetComponentLocation());
	const FVector LocalDelta(LocalGoal.X - LocalEnemy.X, LocalGoal.Y - LocalEnemy.Y, 0.0f);
	if (LocalDelta.SizeSquared2D() <= FMath::Square(AcceptanceRadius))
	{
		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		Enemy->MarkGoalDeckWaypointReached();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	const FVector WorldDirection = DeckTransform.TransformVectorNoScale(LocalDelta.GetSafeNormal2D());
	Enemy->AddMovementInput(WorldDirection, 1.0f);
}

EBTNodeResult::Type UBTT_MoveToDeckWaypoint::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		if (ADeckRangedEnemy* Enemy = Cast<ADeckRangedEnemy>(Controller->GetPawn()))
		{
			if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
		}
	}
	return EBTNodeResult::Aborted;
}
