// Fill out your copyright notice in the Description page of Project Settings.


// Enemy

#include "task/BTT_EnemyBasicAttack.h"

#include "BaseEnemy.h"
#include "BaseAIController.h"

// GASCore
#include "BaseGameplayTags.h"
// Unreal Folder
#include "Engine/Engine.h"

UBTT_EnemyBasicAttack::UBTT_EnemyBasicAttack()
{
	NodeName = TEXT("Enemy Basic Attack");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTT_EnemyBasicAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bSawAttackTag = false;
	
	ABaseAIController* AIController = Cast<ABaseAIController>(OwnerComp.GetOwner());
	if (!AIController) return EBTNodeResult::Failed;
	
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(AIController->GetPawn());
	if (!Enemy) return EBTNodeResult::Failed;
	
	FGameplayTag BasicAttackTag = GameplayAbility_BasicAttack;
	FGameplayTagContainer TagContainer(BasicAttackTag);

	// 태그를 기반으로 어빌리티 실행 시도
	bool bActivated = Enemy->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(TagContainer);

	if (bActivated)
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy Basic Attack Activated"));
		return EBTNodeResult::InProgress;
	}
	
	// 쿨다운, 코스트 부족, 또는 해당 태그를 가진 어빌리티가 없어서 실패한 경우
	return EBTNodeResult::Failed;
}

void UBTT_EnemyBasicAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	ABaseAIController* AIController = Cast<ABaseAIController>(OwnerComp.GetOwner());
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(AIController->GetPawn());

	if (!Enemy || !Enemy->GetAbilitySystemComponent())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const bool bIsAttacking = Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Attacking);

	if (bIsAttacking)
	{
		bSawAttackTag = true;
	}
	else if (bSawAttackTag && !bIsAttacking)
	{
		// 공격이 완전히 끝났음
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
