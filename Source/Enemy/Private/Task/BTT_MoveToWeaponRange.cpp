#include "Task/BTT_MoveToWeaponRange.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Weapon/BaseWeaponComponent.h"

UBTT_MoveToWeaponRange::UBTT_MoveToWeaponRange(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("Move To Weapon Range");
	bCreateNodeInstance = true;

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
	const AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	const float AttackRange = WeaponComponent ? WeaponComponent->GetCurrentAttackRange() : 0.0f;

	if (!Enemy || !WeaponComponent || !WeaponComponent->IsWeaponEquipped()
		|| !IsValid(TargetActor) || AttackRange <= KINDA_SMALL_NUMBER)
	{
		return EBTNodeResult::Failed;
	}

	AcceptableRadius = AttackRange;
	return Super::ExecuteTask(OwnerComp, NodeMemory);
}

FString UBTT_MoveToWeaponRange::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Move to %s\nAcceptance radius: Current Weapon AttackRange"),
		*GetSelectedBlackboardKey().ToString());
}
