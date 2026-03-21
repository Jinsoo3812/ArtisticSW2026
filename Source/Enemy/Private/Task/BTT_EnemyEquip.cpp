// Fill out your copyright notice in the Description page of Project Settings.


#include "Task/BTT_EnemyEquip.h"
#include "BaseAIController.h"
#include "BaseEnemy.h"

UBTT_EnemyEquip::UBTT_EnemyEquip()
{
	NodeName = TEXT("Equip Weapon");
}

EBTNodeResult::Type UBTT_EnemyEquip::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ABaseAIController* AIC = Cast<ABaseAIController>(OwnerComp.GetAIOwner());

	if (!AIC)
	{
		return EBTNodeResult::Failed;
	}

	ABaseEnemy* Enemy = Cast<ABaseEnemy>(AIC->GetPawn());
	if (!Enemy)
	{
		return EBTNodeResult::Failed;
	}

	if (!EquipAbilityClass)
	{
		return EBTNodeResult::Failed;
	}

	IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Enemy);
	if (!AbilitySystemInterface)
	{
		return EBTNodeResult::Failed;
	}

	UAbilitySystemComponent* ASC = AbilitySystemInterface->GetAbilitySystemComponent();
	if (!ASC)
	{
		return EBTNodeResult::Failed;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(EquipAbilityClass);
	return bActivated ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
