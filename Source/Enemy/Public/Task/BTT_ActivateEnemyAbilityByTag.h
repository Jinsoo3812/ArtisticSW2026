#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayAbilitySpec.h"

#include "BTT_ActivateEnemyAbilityByTag.generated.h"

class UAbilitySystemComponent;
class UBehaviorTreeComponent;
struct FAbilityEndedData;

/**
 * Shared server-authoritative BT entry point for Enemy Gameplay Abilities.
 *
 * Designers select the ability by its exact asset tag. The task activates the
 * matching granted spec and waits for that exact spec to end, which also makes
 * abilities that finish synchronously safe to use from Behavior Trees.
 */
UCLASS()
class ENEMY_API UBTT_ActivateEnemyAbilityByTag : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_ActivateEnemyAbilityByTag();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

	FGameplayTag GetAbilityAssetTag() const { return AbilityAssetTag; }
	bool GetCancelAbilityOnAbort() const { return bCancelAbilityOnAbort; }

protected:
	/** Extension point for Boss weapon-source preference or other archetype policies. */
	virtual const FGameplayAbilitySpec* FindAbilitySpec(
		APawn& Pawn,
		const UAbilitySystemComponent& AbilitySystem) const;

	/** Called after the common authority/ASC/tag checks and before selecting a spec. */
	virtual bool ValidateActivationContext(
		APawn& Pawn,
		const UAbilitySystemComponent& AbilitySystem) const;

	/** Allows an archetype to let committed atomic actions finish after BT preconditions change. */
	virtual bool ShouldCancelAbilityOnAbort(const FGameplayAbilitySpec* ActiveSpec) const;

	/** Archetype cleanup hook invoked once for success, failure, or abort. */
	virtual void OnAbilityTaskFinished(EBTNodeResult::Type Result);

	UBehaviorTreeComponent* GetCachedOwnerComp() const { return CachedOwnerComp.Get(); }
	APawn* GetCachedPawn() const { return CachedPawn.Get(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "GameplayAbility"))
	FGameplayTag AbilityAssetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	bool bCancelAbilityOnAbort = true;

private:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	void FinishAbilityTask(EBTNodeResult::Type Result);
	void NotifyTaskFinishedOnce(EBTNodeResult::Type Result);
	void Cleanup();

	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<APawn> CachedPawn;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FGameplayAbilitySpecHandle ActiveAbilityHandle;
	FDelegateHandle AbilityEndedDelegateHandle;
	bool bExecutingActivation = false;
	bool bEndedDuringActivation = false;
	bool bEndedDuringActivationCancelled = false;
	bool bAborting = false;
	bool bCompletionNotified = false;
};
