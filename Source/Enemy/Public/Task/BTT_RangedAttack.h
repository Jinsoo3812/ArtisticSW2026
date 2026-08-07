#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "GameplayAbilitySpecHandle.h"

#include "BTT_RangedAttack.generated.h"

class ARangedEnemy;
class UAbilitySystemComponent;

/**
 * Bridges a Combat subtree to ARangedEnemy's server-authoritative attack API.
 * The task reads TargetActor from Blackboard, starts one GAS ability spec, and
 * completes when that exact ability activation ends.
 */
UCLASS()
class ENEMY_API UBTT_RangedAttack : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_RangedAttack();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

protected:
	/** Cancel ranged abilities when State routing aborts this Combat subtree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	bool bCancelAbilityOnAbort = true;

private:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	EBTNodeResult::Type TryActivateCachedAttack();
	void FinishAttackTask(EBTNodeResult::Type Result);
	void UnregisterAbilityEnded();
	void CleanupTaskState(bool bResetLifecycle = true);

	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedAbilitySystem;
	TWeakObjectPtr<ARangedEnemy> CachedEnemy;
	TWeakObjectPtr<AActor> CachedTarget;
	FGameplayAbilitySpecHandle CachedAbilityHandle;
	FDelegateHandle AbilityEndedDelegateHandle;
	EBTNodeResult::Type SynchronousResult = EBTNodeResult::Failed;
	bool bActivatingAbility = false;
	bool bCompletedSynchronously = false;
	bool bWaitingForCooldown = false;
	bool bAborting = false;
	bool bTaskFinished = false;
};
