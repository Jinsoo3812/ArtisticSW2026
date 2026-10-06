#include "Task/BTT_MoveAroundDeckTarget.h"
#include "AIController.h"
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
	Cleanup(); Elapsed = RetryRemaining = SegmentRemaining = 0.0f;
	Enemy = OwnerComp.GetAIOwner() ? Cast<ADeckEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	Target = OwnerComp.GetBlackboardComponent() ? Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Enemy.IsValid() || !Enemy->CanMoveOnDeck() || !Enemy->IsValidCombatTarget(Target.Get())) { Cleanup(); return EBTNodeResult::Failed; }
	if (!Enemy->GetDeckCombatComponent()->IsCoolingDown()) { Cleanup(); return EBTNodeResult::Succeeded; }
	Direction = Enemy->GetDeckRandomStream().RandRange(0, 1) == 0 ? -1.0f : 1.0f;
	Enemy->SetBaseMovementSpeed(MoveSpeed);
	Enemy->GetDeckCombatComponent()->AcquireFocus(); bOwnsFocus = true;
	PlanNextSegment();
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
	Direction *= -1.0f;
	RetryRemaining = 0.3f;
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
	RetryRemaining -= DeltaSeconds;
	if (RetryRemaining > 0.0f) return;
	UDeckWalkRouteComponent* Route = Enemy->GetDeckWalkRouteComponent();
	SegmentRemaining -= DeltaSeconds;
	if (!Route->HasGoal()) { PlanNextSegment(); return; }
	Enemy->GetDeckEnemyNavigationComponent()->ReplanIfTargetMoved(Target.Get());
	const EDeckWalkRouteTick Result = Route->TickRoute(DeltaSeconds, 30.0f, 1.0f, MaximumDuration, MoveSpeed, 10.0f);
	if (Result != EDeckWalkRouteTick::Moving || SegmentRemaining <= 0.0f)
	{
		Enemy->GetCharacterMovement()->StopMovementImmediately();
		Enemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
		Direction *= -1.0f; PlanNextSegment();
	}
}
void UBTT_MoveAroundDeckTarget::Cleanup()
{
	if (Enemy.IsValid())
	{
		Enemy->GetDeckEnemyNavigationComponent()->CancelCombatRoute();
		if (Enemy->GetCharacterMovement()) Enemy->GetCharacterMovement()->StopMovementImmediately();
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
