#include "Task/BTT_RetreatToWeaponRange.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "RangedEnemy/RangedEnemy.h"
#include "Weapon/BaseWeaponComponent.h"

namespace
{
	constexpr float CandidateAngles[] = { 0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f };

	float CalculatePathLength(const UNavigationPath& Path)
	{
		float Length = 0.0f;
		const TArray<FVector>& Points = Path.PathPoints;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector::Dist2D(Points[Index - 1], Points[Index]);
		}
		return Length;
	}
}

UBTT_RetreatToWeaponRange::UBTT_RetreatToWeaponRange()
{
	NodeName = TEXT("Retreat To Weapon Range");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_RetreatToWeaponRange, BlackboardKey),
		AActor::StaticClass());
}

EBTNodeResult::Type UBTT_RetreatToWeaponRange::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	ResetRuntimeState();

	ARangedEnemy* Enemy = nullptr;
	AActor* Target = nullptr;
	float WeaponRange = 0.0f;
	float DesiredRange = 0.0f;
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller
		|| !ResolveContext(OwnerComp, Enemy, Target, WeaponRange, DesiredRange))
	{
		StopMovement(OwnerComp);
		return EBTNodeResult::Failed;
	}

	const float CurrentDistance = FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation());
	if (CurrentDistance >= DesiredRange - FMath::Max(1.0f, AcceptanceRadius))
	{
		StopMovement(OwnerComp);
		return EBTNodeResult::Succeeded;
	}

	FVector Destination;
	if (!FindBestRetreatDestination(*Enemy, *Target, WeaponRange, DesiredRange, Destination)
		|| !RequestMove(*Controller, Destination))
	{
		StopMovement(OwnerComp);
		return EBTNodeResult::Failed;
	}

	ActiveEnemy = Enemy;
	ActiveTarget = Target;
	LastTargetLocation = Target->GetActorLocation();
	ProgressAnchorDistance = CurrentDistance;
	return EBTNodeResult::InProgress;
}

void UBTT_RetreatToWeaponRange::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	ARangedEnemy* Enemy = nullptr;
	AActor* Target = nullptr;
	float WeaponRange = 0.0f;
	float DesiredRange = 0.0f;
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller
		|| !ResolveContext(OwnerComp, Enemy, Target, WeaponRange, DesiredRange)
		|| Enemy != ActiveEnemy.Get()
		|| Target != ActiveTarget.Get())
	{
		StopMovement(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const float CurrentDistance = FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation());
	if (CurrentDistance >= DesiredRange - FMath::Max(1.0f, AcceptanceRadius))
	{
		StopMovement(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	ElapsedTime += DeltaSeconds;
	TimeSinceProgress += DeltaSeconds;
	TimeSinceRepath += DeltaSeconds;
	if (CurrentDistance >= ProgressAnchorDistance + FMath::Max(1.0f, MinimumProgressDistance))
	{
		ProgressAnchorDistance = CurrentDistance;
		TimeSinceProgress = 0.0f;
	}
	if (ElapsedTime >= FMath::Max(0.1f, MaximumMoveTime)
		|| TimeSinceProgress >= FMath::Max(0.1f, ProgressTimeout))
	{
		StopMovement(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const bool bTargetMoved = FVector::DistSquared2D(LastTargetLocation, Target->GetActorLocation())
		>= FMath::Square(FMath::Max(1.0f, TargetMovementRepathThreshold));
	const bool bMoveStopped = Controller->GetMoveStatus() == EPathFollowingStatus::Idle;
	if (TimeSinceRepath >= FMath::Max(0.05f, RepathInterval) && (bTargetMoved || bMoveStopped))
	{
		FVector Destination;
		if (!FindBestRetreatDestination(*Enemy, *Target, WeaponRange, DesiredRange, Destination)
			|| !RequestMove(*Controller, Destination))
		{
			StopMovement(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		LastTargetLocation = Target->GetActorLocation();
		TimeSinceRepath = 0.0f;
	}
}

EBTNodeResult::Type UBTT_RetreatToWeaponRange::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	StopMovement(OwnerComp);
	ResetRuntimeState();
	return EBTNodeResult::Aborted;
}

void UBTT_RetreatToWeaponRange::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	StopMovement(OwnerComp);
	ResetRuntimeState();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTT_RetreatToWeaponRange::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Retreat from %s to Weapon AttackRange - %.0f cm\nAcceptance %.0f cm, repath %.2f s"),
		*GetSelectedBlackboardKey().ToString(),
		RangeInset,
		AcceptanceRadius,
		RepathInterval);
}

float UBTT_RetreatToWeaponRange::ResolveDesiredRange(
	float WeaponAttackRange,
	float InRangeInset,
	float InMinimumDesiredRange)
{
	const float SafeWeaponRange = FMath::Max(0.0f, WeaponAttackRange);
	const float InsetRange = FMath::Max(0.0f, SafeWeaponRange - FMath::Max(0.0f, InRangeInset));
	return FMath::Min(
		SafeWeaponRange,
		FMath::Max(InsetRange, FMath::Max(0.0f, InMinimumDesiredRange)));
}

FVector UBTT_RetreatToWeaponRange::ResolvePlanarAwayDirection(
	const FVector& EnemyLocation,
	const FVector& TargetLocation,
	const FVector& TargetForward,
	const FVector& EnemyForward)
{
	FVector AwayDirection = EnemyLocation - TargetLocation;
	AwayDirection.Z = 0.0f;
	if (AwayDirection.Normalize())
	{
		return AwayDirection;
	}

	AwayDirection = -TargetForward;
	AwayDirection.Z = 0.0f;
	if (AwayDirection.Normalize())
	{
		return AwayDirection;
	}

	AwayDirection = -EnemyForward;
	AwayDirection.Z = 0.0f;
	return AwayDirection.GetSafeNormal(SMALL_NUMBER, FVector::BackwardVector);
}

bool UBTT_RetreatToWeaponRange::ResolveContext(
	UBehaviorTreeComponent& OwnerComp,
	ARangedEnemy*& OutEnemy,
	AActor*& OutTarget,
	float& OutWeaponRange,
	float& OutDesiredRange) const
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	OutEnemy = Controller ? Cast<ARangedEnemy>(Controller->GetPawn()) : nullptr;
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	OutTarget = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	const UBaseWeaponComponent* WeaponComponent = OutEnemy ? OutEnemy->GetWeaponComponent() : nullptr;
	OutWeaponRange = OutEnemy ? OutEnemy->GetEffectiveAttackRange() : 0.0f;
	const float EffectiveMinimumRange = OutEnemy
		? FMath::Max(MinimumDesiredRange, OutEnemy->GetMinAttackRange() + AcceptanceRadius)
		: MinimumDesiredRange;
	OutDesiredRange = ResolveDesiredRange(OutWeaponRange, RangeInset, EffectiveMinimumRange);
	return OutEnemy
		&& OutEnemy->HasAuthority()
		&& WeaponComponent
		&& WeaponComponent->IsWeaponEquipped()
		&& OutEnemy->CanEngageActor(OutTarget)
		&& OutWeaponRange > EffectiveMinimumRange + KINDA_SMALL_NUMBER
		&& OutDesiredRange > OutEnemy->GetMinAttackRange() + KINDA_SMALL_NUMBER;
}

bool UBTT_RetreatToWeaponRange::FindBestRetreatDestination(
	const ARangedEnemy& Enemy,
	const AActor& Target,
	float WeaponRange,
	float DesiredRange,
	FVector& OutDestination) const
{
	UWorld* World = Enemy.GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (!World || !NavigationSystem)
	{
		return false;
	}

	const FVector EnemyLocation = Enemy.GetActorLocation();
	const FVector TargetLocation = Target.GetActorLocation();
	const float CurrentDistance = FVector::Dist2D(EnemyLocation, TargetLocation);
	const FVector AwayDirection = ResolvePlanarAwayDirection(
		EnemyLocation,
		TargetLocation,
		Target.GetActorForwardVector(),
		Enemy.GetActorForwardVector());

	bool bFoundCandidate = false;
	float BestScore = -TNumericLimits<float>::Max();
	for (const float CandidateAngle : CandidateAngles)
	{
		const FVector CandidateDirection = AwayDirection.RotateAngleAxis(CandidateAngle, FVector::UpVector);
		const FVector RawDestination = TargetLocation + CandidateDirection * DesiredRange;
		FNavLocation ProjectedDestination;
		if (!NavigationSystem->ProjectPointToNavigation(
			RawDestination,
			ProjectedDestination,
			NavigationProjectionExtent))
		{
			continue;
		}

		const float CandidateTargetDistance = FVector::Dist2D(ProjectedDestination.Location, TargetLocation);
		if (CandidateTargetDistance <= CurrentDistance + FMath::Max(1.0f, MinimumProgressDistance)
			|| CandidateTargetDistance > WeaponRange + FMath::Max(1.0f, AcceptanceRadius)
			|| CandidateTargetDistance < DesiredRange - FMath::Max(1.0f, AcceptanceRadius))
		{
			continue;
		}

		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			EnemyLocation,
			ProjectedDestination.Location,
			const_cast<ARangedEnemy*>(&Enemy));
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
		{
			continue;
		}

		const float RangeError = FMath::Abs(CandidateTargetDistance - DesiredRange);
		const float PathLength = CalculatePathLength(*Path);
		const float DirectionAlignment = FVector::DotProduct(
			CandidateDirection.GetSafeNormal2D(),
			AwayDirection);
		const float Score = DirectionAlignment * 500.0f - RangeError - PathLength * 0.05f;
		if (!bFoundCandidate || Score > BestScore)
		{
			bFoundCandidate = true;
			BestScore = Score;
			OutDestination = ProjectedDestination.Location;
		}
	}

	return bFoundCandidate;
}

bool UBTT_RetreatToWeaponRange::RequestMove(
	AAIController& Controller,
	const FVector& Destination)
{
	const EPathFollowingRequestResult::Type Result = Controller.MoveToLocation(
		Destination,
		FMath::Max(1.0f, AcceptanceRadius),
		false,
		true,
		false,
		true,
		nullptr,
		false);
	return Result != EPathFollowingRequestResult::Failed;
}

void UBTT_RetreatToWeaponRange::StopMovement(UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ARangedEnemy* Enemy = Controller ? Cast<ARangedEnemy>(Controller->GetPawn()) : nullptr;
	if (Controller)
	{
		Controller->StopMovement();
	}
	if (Enemy && Enemy->GetCharacterMovement())
	{
		Enemy->GetCharacterMovement()->StopMovementImmediately();
	}
}

void UBTT_RetreatToWeaponRange::ResetRuntimeState()
{
	ActiveEnemy.Reset();
	ActiveTarget.Reset();
	LastTargetLocation = FVector::ZeroVector;
	ElapsedTime = 0.0f;
	TimeSinceProgress = 0.0f;
	TimeSinceRepath = 0.0f;
	ProgressAnchorDistance = 0.0f;
}
