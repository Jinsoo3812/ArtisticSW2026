// Fill out your copyright notice in the Description page of Project Settings.

#include "Task/BTT_StrafeAroundTarget.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

UBTT_StrafeAroundTarget::UBTT_StrafeAroundTarget()
{
	NodeName = TEXT("Strafe Around Target");
	bNotifyTick = true;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_StrafeAroundTarget, BlackboardKey), AActor::StaticClass());
}

EBTNodeResult::Type UBTT_StrafeAroundTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FEnemyStrafeTaskMemory* Memory = CastInstanceNodeMemory<FEnemyStrafeTaskMemory>(NodeMemory);
	if (!Memory)
	{
		return EBTNodeResult::Failed;
	}

	Memory->Reset();
	Memory->DirectionSign = bRandomizeDirection
		? (FMath::RandBool() ? 1 : -1)
		: (bClockwise ? -1 : 1);

	if (!RequestStrafeMove(OwnerComp, *Memory))
	{
		return EBTNodeResult::Failed;
	}

	return EBTNodeResult::InProgress;
}

void UBTT_StrafeAroundTarget::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FEnemyStrafeTaskMemory* Memory = CastInstanceNodeMemory<FEnemyStrafeTaskMemory>(NodeMemory);
	if (!Memory)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Memory->ElapsedTime += DeltaSeconds;
	Memory->TimeSinceLastMoveRequest += DeltaSeconds;

	if (StrafeDuration > 0.0f && Memory->ElapsedTime >= StrafeDuration)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (Memory->TimeSinceLastMoveRequest >= MoveRequestInterval)
	{
		if (!RequestStrafeMove(OwnerComp, *Memory))
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}
}

EBTNodeResult::Type UBTT_StrafeAroundTarget::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (bStopMovementOnFinish)
	{
		if (AAIController* AIController = OwnerComp.GetAIOwner())
		{
			AIController->StopMovement();
		}
	}

	return Super::AbortTask(OwnerComp, NodeMemory);
}

void UBTT_StrafeAroundTarget::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (bStopMovementOnFinish)
	{
		if (AAIController* AIController = OwnerComp.GetAIOwner())
		{
			AIController->StopMovement();
		}
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTT_StrafeAroundTarget::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FEnemyStrafeTaskMemory>(NodeMemory, InitType);
}

void UBTT_StrafeAroundTarget::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FEnemyStrafeTaskMemory>(NodeMemory, CleanupType);
}

FString UBTT_StrafeAroundTarget::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s: radius %.0f, duration %.2fs, target %s"),
		*Super::GetStaticDescription(),
		DesiredRadius,
		StrafeDuration,
		*BlackboardKey.SelectedKeyName.ToString());
}

bool UBTT_StrafeAroundTarget::RequestStrafeMove(UBehaviorTreeComponent& OwnerComp, FEnemyStrafeTaskMemory& Memory) const
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return false;
	}

	FVector Destination;
	if (!CalculateStrafeDestination(OwnerComp, Memory, Destination))
	{
		return false;
	}

	Memory.TimeSinceLastMoveRequest = 0.0f;

	const EPathFollowingRequestResult::Type MoveResult = AIController->MoveToLocation(
		Destination,
		AcceptanceRadius,
		false,
		bUsePathfinding,
		bProjectDestinationToNavigation,
		true,
		nullptr,
		bAllowPartialPath);

	return MoveResult != EPathFollowingRequestResult::Failed;
}

bool UBTT_StrafeAroundTarget::CalculateStrafeDestination(UBehaviorTreeComponent& OwnerComp, const FEnemyStrafeTaskMemory& Memory, FVector& OutDestination) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const APawn* Pawn = AIController ? AIController->GetPawn() : nullptr;
	const AActor* TargetActor = GetTargetActor(OwnerComp);
	if (!Pawn || !TargetActor)
	{
		return false;
	}

	const FVector TargetLocation = TargetActor->GetActorLocation();
	const FVector PawnLocation = Pawn->GetActorLocation();

	FVector RadialDirection = PawnLocation - TargetLocation;
	RadialDirection.Z = 0.0f;
	if (!RadialDirection.Normalize())
	{
		RadialDirection = Pawn->GetActorForwardVector();
		RadialDirection.Z = 0.0f;
		RadialDirection.Normalize();
	}

	const float SignedAngleRadians = FMath::DegreesToRadians(LookAheadAngleDegrees) * static_cast<float>(Memory.DirectionSign);
	const FVector NextRadialDirection = RadialDirection.RotateAngleAxis(FMath::RadiansToDegrees(SignedAngleRadians), FVector::UpVector).GetSafeNormal();

	const float Radius = FMath::Max(0.0f, DesiredRadius);
	const FVector DesiredDestination = TargetLocation + (NextRadialDirection * Radius);

	if (!bProjectDestinationToNavigation)
	{
		OutDestination = DesiredDestination;
		return true;
	}

	return ProjectDestinationToNavigation(OwnerComp, DesiredDestination, OutDestination);
}

AActor* UBTT_StrafeAroundTarget::GetTargetActor(UBehaviorTreeComponent& OwnerComp) const
{
	UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	return BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
}

bool UBTT_StrafeAroundTarget::ProjectDestinationToNavigation(UBehaviorTreeComponent& OwnerComp, const FVector& DesiredDestination, FVector& OutProjectedDestination) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const APawn* Pawn = AIController ? AIController->GetPawn() : nullptr;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UNavigationSystemV1* NavigationSystem = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(DesiredDestination, ProjectedLocation))
	{
		return false;
	}

	OutProjectedDestination = ProjectedLocation.Location;
	return true;
}
