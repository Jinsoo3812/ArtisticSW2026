#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayAbilitySpec.h"
#include "BTT_ActivateBossAbility.generated.h"

class UAbilitySystemComponent;
class UBehaviorTreeComponent;
struct FAbilityEndedData;

/** Activates any boss ASC ability by asset tag and completes when that exact spec ends. */
UCLASS()
class ENEMY_API UBTT_ActivateBossAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_ActivateBossAbility();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	void Cleanup();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ability")
	FGameplayTag AbilityAssetTag;

private:
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FGameplayAbilitySpecHandle ActiveAbilityHandle;
	FDelegateHandle AbilityEndedDelegateHandle;
	bool bExecutingActivation = false;
	bool bEndedDuringActivation = false;
	bool bEndedDuringActivationCancelled = false;
};
