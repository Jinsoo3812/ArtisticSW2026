#include "Task/BTT_MoveToWeaponRange.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapon/BaseWeaponComponent.h"

UBTT_MoveToWeaponRange::UBTT_MoveToWeaponRange(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("Move To Weapon Range");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	bAllowStrafe = false;
	bTrackMovingGoal = true;
	bReachTestIncludesAgentRadius = false;
	bReachTestIncludesGoalRadius = false;
}

EBTNodeResult::Type UBTT_MoveToWeaponRange::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	const UBaseWeaponComponent* WeaponComponent = Enemy ? Enemy->GetWeaponComponent() : nullptr;
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	const float AttackRange = WeaponComponent ? WeaponComponent->GetCurrentAttackRange() : 0.0f;

	if (!Enemy || !Enemy->HasAuthority() || !WeaponComponent || !WeaponComponent->IsWeaponEquipped()
		|| !Enemy->CanEngageActor(TargetActor)
		|| AttackRange <= KINDA_SMALL_NUMBER)
	{
		return EBTNodeResult::Failed;
	}

	AcceptableRadius = ResolveAcceptanceRange(
		AttackRange,
		AcceptanceRangeInset,
		MinimumAcceptanceRange);
	return Super::ExecuteTask(OwnerComp, NodeMemory);
}

void UBTT_MoveToWeaponRange::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	if (AIController)
	{
		AIController->StopMovement();
	}
	if (Enemy && Enemy->GetCharacterMovement())
	{
		Enemy->GetCharacterMovement()->StopMovementImmediately();
	}
}

FString UBTT_MoveToWeaponRange::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Move to %s\nAcceptance: Weapon AttackRange - %.0f cm (minimum %.0f cm)"),
		*GetSelectedBlackboardKey().ToString(),
		AcceptanceRangeInset,
		MinimumAcceptanceRange);
}

float UBTT_MoveToWeaponRange::ResolveAcceptanceRange(
	float WeaponAttackRange,
	float Inset,
	float MinimumRange)
{
	const float SafeAttackRange = FMath::Max(0.0f, WeaponAttackRange);
	const float InsetRange = FMath::Max(0.0f, SafeAttackRange - FMath::Max(0.0f, Inset));
	return FMath::Min(
		SafeAttackRange,
		FMath::Max(InsetRange, FMath::Max(0.0f, MinimumRange)));
}
