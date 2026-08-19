#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "BTT_ActivateEnemyShipAbility.generated.h"

class UAbilitySystemComponent;

UCLASS()
class ENEMY_API UBTT_ActivateEnemyShipAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_ActivateEnemyShipAbility();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	FName GetSelectedAbilityTagKeyName() const { return SelectedAbilityTagKey.SelectedKeyName; }
	FName GetSelectedRuleIdKeyName() const { return SelectedRuleIdKey.SelectedKeyName; }
	bool GetCancelAbilityOnAbort() const { return bCancelAbilityOnAbort; }

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SelectedAbilityTagKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SelectedRuleIdKey;

	UPROPERTY(EditAnywhere, Category = "Ability")
	bool bCancelAbilityOnAbort = true;

private:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	void FinishAbilityTask(EBTNodeResult::Type Result);
	void Cleanup();

	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FGameplayAbilitySpecHandle ActiveHandle;
	FDelegateHandle AbilityEndedHandle;
	bool bActivating = false;
	bool bCompletedSynchronously = false;
	bool bSynchronousCancelled = false;
	bool bAborting = false;
};
