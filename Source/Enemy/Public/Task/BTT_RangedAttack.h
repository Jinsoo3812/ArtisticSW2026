#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "GameplayTagContainer.h"

#include "BTT_RangedAttack.generated.h"

class ARangedEnemy;
class UAbilitySystemComponent;

/**
 * Bridges a Combat subtree to ARangedEnemy's server-authoritative attack API.
 * The task reads TargetActor from Blackboard, starts the GAS ability, and waits
 * for the configured execution-state tag to be removed.
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
	/** Tag owned by the attack ability while its montage/projectile sequence is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	FGameplayTag AttackExecutionStateTag;

	/** Cancel ranged abilities when State routing aborts this Combat subtree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	bool bCancelAbilityOnAbort = true;

private:
	void HandleAttackStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	EBTNodeResult::Type TryActivateCachedAttack();
	void FinishAttackTask(EBTNodeResult::Type Result);
	void CleanupTaskState();

	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedAbilitySystem;
	TWeakObjectPtr<ARangedEnemy> CachedEnemy;
	FDelegateHandle AttackTagDelegateHandle;
	bool bObservedAttackStart = false;
	bool bActivatingAbility = false;
	bool bCompletedSynchronously = false;
	bool bWaitingForCooldown = false;
};
