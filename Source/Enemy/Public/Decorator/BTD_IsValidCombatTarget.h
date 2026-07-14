// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"
#include "BTD_IsValidCombatTarget.generated.h"

class UBaseHealthComponent;

/**
 * Checks whether the selected blackboard actor can be used as a combat target.
 *
 * Currently this means "valid actor and not dead". Object pooling can extend this
 * check later with pooled/inactive target state without changing individual BTTasks.
 */
UCLASS()
class ENEMY_API UBTD_IsValidCombatTarget : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_IsValidCombatTarget();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;

	bool IsValidCombatTarget(const AActor* TargetActor) const;
	bool IsTargetAlive(const AActor* TargetActor) const;
	bool IsTargetPoolActive(const AActor* TargetActor) const;

protected:
	UPROPERTY(EditAnywhere, Category = "Target")
	bool bClearInvalidTarget = true;

	UPROPERTY(EditAnywhere, Category = "Target")
	bool bRequireHealthComponent = false;
};
