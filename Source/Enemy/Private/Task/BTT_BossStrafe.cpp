#include "Task/BTT_BossStrafe.h"

#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

UBTT_BossStrafe::UBTT_BossStrafe()
{
	NodeName = TEXT("Boss Timed Strafe");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	TargetActorKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_BossStrafe, TargetActorKey),
		AActor::StaticClass());
}

EBTNodeResult::Type UBTT_BossStrafe::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	ResetRuntimeState();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AShipBossEnemy* Boss = Controller ? Cast<AShipBossEnemy>(Controller->GetPawn()) : nullptr;
	if (Boss && Boss->GetAbilitySystemComponent()
		&& Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy)) return EBTNodeResult::Failed;
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	UCharacterMovementComponent* Movement = Boss ? Boss->GetCharacterMovement() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName))
		: nullptr;
	if (!Target && Boss)
	{
		Target = Boss->GetBossCombatTarget();
	}
	UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, TargetFloor;
	if (!Boss || !Boss->HasAuthority() || !HostShip || !DeckMesh || !Movement || !Area
		|| !Boss->CanEngageActor(Target))
	{
		return EBTNodeResult::Failed;
	}
	if (!Area->ResolveActorOnDeck(*Boss, Start) || !Area->ResolveActorOnDeck(*Target, TargetFloor)
		|| TargetFloor.SurfaceId != Start.SurfaceId)
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Cannot resolve strafe start/target points on the same deck surface."));
		return EBTNodeResult::Failed;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector BossLocal = DeckTransform.InverseTransformPosition(Boss->GetActorLocation());
	const FVector TargetLocal = DeckTransform.InverseTransformPosition(Target->GetActorLocation());
	const FVector FallbackLocalForward = DeckTransform.InverseTransformVectorNoScale(
		Boss->GetActorForwardVector());
	const bool bMoveLeft = bRandomizeDirection ? FMath::RandBool() : bMoveLeftByDefault;
	CachedLocalMoveDirection = CalculateLocalTangent(
		BossLocal, TargetLocal, FallbackLocalForward, bMoveLeft);

	CachedBoss = Boss;
	CachedHostShip = HostShip;
	PreviousBaseMovementSpeed = Boss->GetBaseMovementSpeed();
	bHasPreviousBaseMovementSpeed = true;
	ElapsedMovementTime = 0.0f;
	Boss->SetBaseMovementSpeed(FMath::Max(10.0f, MoveSpeed));
	Movement->SetMovementMode(MOVE_Walking);
	Boss->SetBase(Area->GetMovementBase(*Boss));
	return EBTNodeResult::InProgress;
}

void UBTT_BossStrafe::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	AShipBossEnemy* Boss = CachedBoss.Get();
	AEnemyShip* HostShip = CachedHostShip.Get();
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	if (!Boss || !HostShip || !DeckMesh)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ElapsedMovementTime += DeltaSeconds;
	if (ElapsedMovementTime >= FMath::Clamp(StrafeDuration, 0.05f, 1.0f))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	const UDeckWalkAreaComponent* Area = HostShip->GetDeckWalkAreaComponent();
	const FVector LookAhead = Area ? Area->ToLocal(Area->GetActorFeetWorld(*Boss))
		+ CachedLocalMoveDirection * FMath::Max(75.0f, MoveSpeed * DeltaSeconds) : FVector::ZeroVector;
	FDeckWalkLocation Current, Next;
	if (!Area || !Area->ResolveActorOnDeck(*Boss, Current)
		|| !Area->ResolveLocalFloor(LookAhead, Current.SurfaceId, Next)
		|| !Area->IsSupportedSegment(Current, LookAhead) || !Area->IsLocationAvailable(Next, *Boss))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("No supported and available strafe point."));
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	Boss->SetBase(Area->GetMovementBase(*Boss));

	const FVector WorldDirection = DeckMesh->GetComponentTransform()
		.TransformVectorNoScale(CachedLocalMoveDirection)
		.GetSafeNormal();
	Boss->AddMovementInput(WorldDirection, 1.0f);
}

EBTNodeResult::Type UBTT_BossStrafe::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	StopMovement();
	ResetRuntimeState();
	return EBTNodeResult::Aborted;
}

void UBTT_BossStrafe::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	StopMovement();
	ResetRuntimeState();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTT_BossStrafe::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Apply tangential deck movement for %.2f s at %.0f cm/s; require supported floor and clearance"),
		StrafeDuration,
		MoveSpeed);
}

FVector UBTT_BossStrafe::CalculateLocalTangent(
	const FVector& BossLocalLocation,
	const FVector& TargetLocalLocation,
	const FVector& FallbackLocalForward,
	bool bMoveLeft)
{
	FVector Radial(
		BossLocalLocation.X - TargetLocalLocation.X,
		BossLocalLocation.Y - TargetLocalLocation.Y,
		0.0f);
	Radial = Radial.GetSafeNormal2D();
	if (Radial.IsNearlyZero())
	{
		Radial = FVector(FallbackLocalForward.X, FallbackLocalForward.Y, 0.0f)
			.GetSafeNormal2D();
	}
	if (Radial.IsNearlyZero())
	{
		Radial = FVector::ForwardVector;
	}

	const FVector LeftTangent(-Radial.Y, Radial.X, 0.0f);
	return bMoveLeft ? LeftTangent : -LeftTangent;
}

void UBTT_BossStrafe::StopMovement() const
{
	if (AShipBossEnemy* Boss = CachedBoss.Get())
	{
		if (UCharacterMovementComponent* Movement = Boss->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			if (bHasPreviousBaseMovementSpeed)
			{
				Boss->SetBaseMovementSpeed(PreviousBaseMovementSpeed);
			}
		}
	}
}

void UBTT_BossStrafe::ResetRuntimeState()
{
	CachedBoss.Reset();
	CachedHostShip.Reset();
	CachedLocalMoveDirection = FVector::ZeroVector;
	PreviousBaseMovementSpeed = 0.0f;
	ElapsedMovementTime = 0.0f;
	bHasPreviousBaseMovementSpeed = false;
}
