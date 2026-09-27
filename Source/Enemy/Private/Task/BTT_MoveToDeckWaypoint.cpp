#include "Task/BTT_MoveToDeckWaypoint.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/BossDeckMovementUtils.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

namespace
{
	struct FDeckWaypointMoveMemory
	{
		float ElapsedTime = 0.0f;
		float TimeSinceProgress = 0.0f;
		float ProgressAnchorDistance = TNumericLimits<float>::Max();
		float EffectiveAcceptanceRadius = 0.0f;
		int32 ActiveGoalPointId = INDEX_NONE;
	};

	IDeckWaypointMovementInterface* ResolveDeckMover(const UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* Controller = OwnerComp.GetAIOwner();
		return Controller ? Cast<IDeckWaypointMovementInterface>(Controller->GetPawn()) : nullptr;
	}

	ACharacter* ResolveMovingCharacter(const UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* Controller = OwnerComp.GetAIOwner();
		return Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	}

	void StopDeckMovement(UBehaviorTreeComponent& OwnerComp, ACharacter* Character)
	{
		if (AAIController* Controller = OwnerComp.GetAIOwner())
		{
			Controller->StopMovement();
		}
		if (Character)
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
		}
	}

	void ClearDestinationBlackboard(UBehaviorTreeComponent& OwnerComp)
	{
		static const FName DestinationPointKey(TEXT("DestinationPointId"));
		if (UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
			Blackboard && Blackboard->GetKeyID(DestinationPointKey) != FBlackboard::InvalidKey)
		{
			Blackboard->SetValueAsInt(DestinationPointKey, INDEX_NONE);
		}
	}

	void ResetMoveMemory(
		FDeckWaypointMoveMemory& Memory,
		const FVector& LocalCharacter,
		const FVector& LocalGoal,
		float AcceptanceRadius,
		int32 GoalPointId)
	{
		const float InitialDistance = FVector::Dist2D(LocalCharacter, LocalGoal);
		Memory = FDeckWaypointMoveMemory();
		Memory.ProgressAnchorDistance = InitialDistance;
		Memory.EffectiveAcceptanceRadius = BossDeckMovement::ResolveAcceptanceRadius(
			AcceptanceRadius, InitialDistance);
		Memory.ActiveGoalPointId = GoalPointId;
	}
}

UBTT_MoveToDeckWaypoint::UBTT_MoveToDeckWaypoint()
{
	NodeName = TEXT("Move To Live Deck Waypoint");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTT_MoveToDeckWaypoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	IDeckWaypointMovementInterface* DeckMover = ResolveDeckMover(OwnerComp);
	ACharacter* Character = ResolveMovingCharacter(OwnerComp);
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(Character);
	ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(Character);
	if (DeckEnemy && DeckEnemy->GetGoalDeckWaypointId() == INDEX_NONE)
	{
		if (UDeckEnemyNavigationComponent* Navigation = DeckEnemy->GetDeckEnemyNavigationComponent())
		{
			Navigation->PrepareNextHop();
		}
	}
	AEnemyShip* HostShip = DeckMover ? DeckMover->GetDeckHostShip() : nullptr;
	const UDeckWaypointComponent* Goal = HostShip && DeckMover
		? HostShip->GetDeckWaypoint(DeckMover->GetGoalDeckPointId())
		: nullptr;
	if (!DeckMover || !Character || !Enemy || !Enemy->HasAuthority()
		|| !DeckMover->CanMoveOnDeck() || !HostShip
		|| !HostShip->GetShipDeckMesh() || !Goal)
	{
		StopDeckMovement(OwnerComp, Character);
		if (DeckMover)
		{
			DeckMover->OnDeckMoveFailed();
		}
		ClearDestinationBlackboard(OwnerComp);
		return EBTNodeResult::Failed;
	}

	Character->SetBase(HostShip->GetShipDeckMesh());
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Enemy->SetBaseMovementSpeed(MoveSpeed);
		Movement->BrakingDecelerationWalking = BrakingDeceleration;
		if (!Movement->IsMovingOnGround())
		{
			StopDeckMovement(OwnerComp, Character);
			DeckMover->OnDeckMoveFailed();
			ClearDestinationBlackboard(OwnerComp);
			return EBTNodeResult::Failed;
		}
		Movement->bForceNextFloorCheck = true;
	}

	const FTransform DeckTransform = HostShip->GetShipDeckMesh()->GetComponentTransform();
	const FVector LocalCharacter = DeckTransform.InverseTransformPosition(Character->GetActorLocation());
	const FVector LocalGoal = DeckTransform.InverseTransformPosition(Goal->GetComponentLocation());
	FDeckWaypointMoveMemory& Memory = *reinterpret_cast<FDeckWaypointMoveMemory*>(NodeMemory);
	ResetMoveMemory(
		Memory, LocalCharacter, LocalGoal, AcceptanceRadius, Goal->GetWaypointId());
	return EBTNodeResult::InProgress;
}

void UBTT_MoveToDeckWaypoint::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	IDeckWaypointMovementInterface* DeckMover = ResolveDeckMover(OwnerComp);
	ACharacter* Character = ResolveMovingCharacter(OwnerComp);
	ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(Character);
	AEnemyShip* HostShip = DeckMover ? DeckMover->GetDeckHostShip() : nullptr;
	UDeckEnemyNavigationComponent* CombatNavigation = DeckEnemy
		? DeckEnemy->GetDeckEnemyNavigationComponent()
		: nullptr;
	if (DeckEnemy && DeckEnemy->CanAttackCurrentTarget(true))
	{
		StopDeckMovement(OwnerComp, Character);
		if (CombatNavigation)
		{
			CombatNavigation->CancelCombatRoute();
		}
		ClearDestinationBlackboard(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	if (CombatNavigation && CombatNavigation->HasActiveRoute()
		&& CombatNavigation->ReplanIfTargetMoved(DeckEnemy->GetCombatTarget(), true)
		&& DeckEnemy->GetGoalDeckWaypointId() == INDEX_NONE)
	{
		StopDeckMovement(OwnerComp, Character);
		ClearDestinationBlackboard(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	const UDeckWaypointComponent* Goal = HostShip && DeckMover
		? HostShip->GetDeckWaypoint(DeckMover->GetGoalDeckPointId())
		: nullptr;
	if (!DeckMover || !Character || !DeckMover->CanMoveOnDeck() || !HostShip
		|| !HostShip->GetShipDeckMesh() || !Goal)
	{
		StopDeckMovement(OwnerComp, Character);
		if (DeckMover)
		{
			DeckMover->OnDeckMoveFailed();
		}
		ClearDestinationBlackboard(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FTransform DeckTransform = HostShip->GetShipDeckMesh()->GetComponentTransform();
	const FVector LocalEnemy = DeckTransform.InverseTransformPosition(Character->GetActorLocation());
	const FVector LocalGoal = DeckTransform.InverseTransformPosition(Goal->GetComponentLocation());
	const FVector LocalDelta(LocalGoal.X - LocalEnemy.X, LocalGoal.Y - LocalEnemy.Y, 0.0f);
	const float Distance = LocalDelta.Size2D();
	FDeckWaypointMoveMemory& Memory = *reinterpret_cast<FDeckWaypointMoveMemory*>(NodeMemory);
	if (Memory.ActiveGoalPointId != Goal->GetWaypointId())
	{
		ResetMoveMemory(
			Memory, LocalEnemy, LocalGoal, AcceptanceRadius, Goal->GetWaypointId());
	}
	if (BossDeckMovement::IsWithinPlanarAcceptance(
		LocalEnemy, LocalGoal, Memory.EffectiveAcceptanceRadius))
	{
		StopDeckMovement(OwnerComp, Character);
		const int32 ReachedPointId = DeckMover->GetGoalDeckPointId();
		DeckMover->OnDeckPointReached();
		if (DeckMover->GetCurrentDeckPointId() != ReachedPointId)
		{
			DeckMover->OnDeckMoveFailed();
			if (CombatNavigation)
			{
				CombatNavigation->CancelCombatRoute();
			}
			ClearDestinationBlackboard(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
		if (CombatNavigation && CombatNavigation->HasActiveRoute()
			&& CombatNavigation->HandlePointReached())
		{
			const UDeckWaypointComponent* NextGoal = HostShip->GetDeckWaypoint(
				DeckMover->GetGoalDeckPointId());
			if (NextGoal)
			{
				const FVector NextLocalEnemy = DeckTransform.InverseTransformPosition(
					Character->GetActorLocation());
				const FVector NextLocalGoal = DeckTransform.InverseTransformPosition(
					NextGoal->GetComponentLocation());
				ResetMoveMemory(
					Memory, NextLocalEnemy, NextLocalGoal,
					AcceptanceRadius, NextGoal->GetWaypointId());
				return;
			}
		}
		ClearDestinationBlackboard(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	Memory.ElapsedTime += DeltaSeconds;
	Memory.TimeSinceProgress += DeltaSeconds;
	if (Distance <= Memory.ProgressAnchorDistance - FMath::Max(1.0f, MinimumProgressDistance))
	{
		Memory.ProgressAnchorDistance = Distance;
		Memory.TimeSinceProgress = 0.0f;
	}
	if (Memory.ElapsedTime >= FMath::Max(0.1f, MaximumMoveTime)
		|| Memory.TimeSinceProgress >= FMath::Max(0.1f, ProgressTimeout))
	{
		StopDeckMovement(OwnerComp, Character);
		DeckMover->OnDeckMoveFailed();
		ClearDestinationBlackboard(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FVector WorldDirection = DeckTransform.TransformVectorNoScale(LocalDelta.GetSafeNormal2D());
	Character->AddMovementInput(WorldDirection, 1.0f);
}

EBTNodeResult::Type UBTT_MoveToDeckWaypoint::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	ACharacter* Character = ResolveMovingCharacter(OwnerComp);
	StopDeckMovement(OwnerComp, Character);
	if (IDeckWaypointMovementInterface* DeckMover = ResolveDeckMover(OwnerComp))
	{
		DeckMover->OnDeckMoveFailed();
	}
	if (ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(Character))
	{
		if (UDeckEnemyNavigationComponent* Navigation = DeckEnemy->GetDeckEnemyNavigationComponent())
		{
			Navigation->CancelCombatRoute();
		}
	}
	ClearDestinationBlackboard(OwnerComp);
	return EBTNodeResult::Aborted;
}

uint16 UBTT_MoveToDeckWaypoint::GetInstanceMemorySize() const
{
	return sizeof(FDeckWaypointMoveMemory);
}
