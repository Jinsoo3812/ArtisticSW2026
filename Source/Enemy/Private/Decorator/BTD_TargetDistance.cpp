#include "Decorator/BTD_TargetDistance.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTD_TargetDistance::UBTD_TargetDistance()
{
	NodeName = TEXT("Target Distance");
	FlowAbortMode = EBTFlowAbortMode::Both;
	bTickIntervals = true;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_TargetDistance, BlackboardKey),
		AActor::StaticClass());
}

bool UBTD_TargetDistance::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const APawn* Pawn = AIController ? AIController->GetPawn() : nullptr;
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	const AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	if (!Pawn || !IsValid(TargetActor))
	{
		return false;
	}

	const float ActualDistance = bUse2DDistance
		? FVector::Dist2D(Pawn->GetActorLocation(), TargetActor->GetActorLocation())
		: FVector::Distance(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
	const bool bWithinDistance = ActualDistance <= FMath::Max(0.0f, Distance);
	return Query == ETargetDistanceQuery::Within ? bWithinDistance : !bWithinDistance;
}

void UBTD_TargetDistance::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	SetNextTickTime(NodeMemory, FMath::Max(0.02f, EvaluationInterval));
}

void UBTD_TargetDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	SetNextTickTime(NodeMemory, FMath::Max(0.02f, EvaluationInterval));
}

FString UBTD_TargetDistance::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s is %s %.0f cm (%s)"),
		*GetSelectedBlackboardKey().ToString(),
		Query == ETargetDistanceQuery::Within ? TEXT("within") : TEXT("outside"),
		Distance,
		bUse2DDistance ? TEXT("2D") : TEXT("3D"));
}
