// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_EnemyEquip.generated.h"

class UGameplayAbility;

UCLASS()
class ENEMY_API UBTT_EnemyEquip : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EnemyEquip();

protected:
	/** Enemy에게 미리 부여되어 있어야 하는 장착 Ability 클래스 */
	UPROPERTY(EditAnywhere, Category = "Ability")
	TSubclassOf<UGameplayAbility> EquipAbilityClass;

public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
