#include "Task/BTT_MoveAroundDeckTarget.h"
#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTT_MoveAroundDeckTarget::UBTT_MoveAroundDeckTarget()
{
	NodeName = TEXT("Move Around Player On Deck (Cooldown)");
	bCreateNodeInstance = true; bNotifyTick = bNotifyTaskFinished = true;
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_MoveAroundDeckTarget, BlackboardKey), AActor::StaticClass());
}
EBTNodeResult::Type UBTT_MoveAroundDeckTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Cleanup(); Elapsed = SegmentRemaining = 0.0f;
	Enemy = OwnerComp.GetAIOwner() ? Cast<ADeckEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	Target = OwnerComp.GetBlackboardComponent() ? Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Enemy.IsValid() || !Enemy->CanMoveOnDeck() || !Enemy->IsValidCombatTarget(Target.Get())) { Cleanup(); return EBTNodeResult::Failed; }
	if (!Enemy->GetDeckCombatComponent()->IsCoolingDown()) { Cleanup(); return EBTNodeResult::Succeeded; }
	Direction = Enemy->GetDeckRandomStream().RandRange(0, 1) == 0 ? -1.0f : 1.0f;
	Enemy->SetBaseMovementSpeed(MoveSpeed);
	Enemy->GetDeckCombatComponent()->AcquireFocus(); bOwnsFocus = true;
	if (!PlanNextSegment()) { Cleanup(); return EBTNodeResult::Failed; }
	return EBTNodeResult::InProgress;
}
bool UBTT_MoveAroundDeckTarget::PlanNextSegment()
{
	if (!Enemy.IsValid()) return false;
	UDeckEnemyNavigationComponent* Nav = Enemy->GetDeckEnemyNavigationComponent();
	if (Nav->PlanTargetDistanceRoute(Target.Get(), TargetDistance, ProjectionTolerance, Direction * StrafeAngle))
	{
		SegmentRemaining = 1.5f; return true;
	}
	return false;
}
void UBTT_MoveAroundDeckTarget::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	if (!Enemy.IsValid() || !Enemy->CanMoveOnDeck() || Enemy->GetCombatTarget() != Target.Get() || !Enemy->IsValidCombatTarget(Target.Get()))
	{
		Cleanup(); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return;
	}
	UDeckEnemyCombatComponent* Combat = Enemy->GetDeckCombatComponent();
	Combat->RefreshFocus();
	if (!Combat->IsCoolingDown() || Elapsed >= MaximumDuration)
	{
		Cleanup(); FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded); return;
	}
	UDeckWalkRouteComponent* Route = Enemy->GetDeckWalkRouteComponent();
	SegmentRemaining -= DeltaSeconds;
	if (!Route->HasGoal())
	{
		if (SegmentRemaining <= 0.f)
		{
			Direction *= -1.f;
			if (!PlanNextSegment()) { Cleanup(); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); }
		}
		return;
	}
	Enemy->GetDeckEnemyNavigationComponent()->ReplanIfTargetMoved(Target.Get());
	const EDeckWalkRouteTick Result = Route->TickRoute(DeltaSeconds, 30.0f, 1.0f, MaximumDuration, MoveSpeed, 10.0f);
	if (Result == EDeckWalkRouteTick::Failed || Result == EDeckWalkRouteTick::Blocked)
	{
		EnemyPointSelectionFailure::Log(this, Enemy.Get(), TEXT("Deck strafe route is no longer usable."));
		Cleanup(); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return;
	}
	if (Result != EDeckWalkRouteTick::Moving || SegmentRemaining <= 0.0f)
	{
		Route->StopWalkingMovement();
		Direction *= -1.0f;
		if (Result == EDeckWalkRouteTick::Reached)
		{
			Enemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
			SegmentRemaining = 0.3f;
			return;
		}
		if (!PlanNextSegment()) { Cleanup(); FinishLatentTask(OwnerComp, EBTNodeResult::Failed); }
	}
}
void UBTT_MoveAroundDeckTarget::Cleanup()
{
	if (Enemy.IsValid())
	{
		Enemy->GetDeckWalkRouteComponent()->StopWalkingMovement();
		Enemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
		if (bOwnsFocus) Enemy->GetDeckCombatComponent()->ReleaseFocus();
	}
	bOwnsFocus = false; Enemy.Reset(); Target.Reset();
}
EBTNodeResult::Type UBTT_MoveAroundDeckTarget::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Cleanup(); return EBTNodeResult::Aborted;
}
void UBTT_MoveAroundDeckTarget::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	Cleanup(); Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
}
