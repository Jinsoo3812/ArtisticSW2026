#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "GameplayAbilitySpecHandle.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "BTT_DeckAttack.generated.h"
class UAbilitySystemComponent;
class UDeckEnemyCombatComponent;

/** One weapon ability activation for either deck role, with exact completion/result tracking. */
UCLASS()
class ENEMY_API UBTT_DeckAttack : public UBTTask_BlackboardBase
{
	GENERATED_BODY()
public:
	UBTT_DeckAttack();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
protected:
	/** Completion watchdog. A still-playing committed montage is never cut short by this duration. */
	UPROPERTY(EditAnywhere, Category = "Deck AI|Attack", meta = (ClampMin = "0.1", Units = "s", ToolTip = "Completion watchdog; does not interrupt a still-playing committed attack montage."))
	float MaximumAttackTime = 15.0f;
private:
	void OnAbilityEnded(const FAbilityEndedData& Data);
	void Cleanup();
	void CancelAttack();
	EBTNodeResult::Type GetResult() const;
	TWeakObjectPtr<UBehaviorTreeComponent> Behavior;
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	TWeakObjectPtr<UDeckEnemyCombatComponent> Combat;
	FGameplayAbilitySpecHandle AbilityHandle;
	FDelegateHandle EndHandle;
	uint32 Attempt = 0;
	float Elapsed = 0.0f;
	bool bActivating = false;
	bool bEndedSynchronously = false;
	bool bEnding = false;
};
