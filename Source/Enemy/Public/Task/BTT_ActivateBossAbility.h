#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
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
	FGameplayTag GetAbilityAssetTag() const { return AbilityAssetTag; }
	bool RequiresPreselectedDestination() const { return bRequirePreselectedDestination; }
	bool PrefersCurrentWeaponAbility() const { return bPreferCurrentWeaponAbility; }

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	const FGameplayAbilitySpec* FindAbilitySpec(
		const class AShipBossEnemy& Boss,
		const UAbilitySystemComponent& AbilitySystem) const;
	void ResetDestinationState();
	void Cleanup();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ability")
	FGameplayTag AbilityAssetTag;

	/**
	 * If a matching ability was granted by the equipped weapon, activate that
	 * exact spec instead of an unrelated ability that happens to share a tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ability")
	bool bPreferCurrentWeaponAbility = true;

	/** Mobility branches can require BTT_SelectBossDestinationPoint to run first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	bool bRequirePreselectedDestination = false;

	/** Keeps the server Blackboard and the replicated boss destination from becoming stale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	bool bClearDestinationWhenFinished = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	FBlackboardKeySelector DestinationPointKey;

private:
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FGameplayAbilitySpecHandle ActiveAbilityHandle;
	FDelegateHandle AbilityEndedDelegateHandle;
	bool bExecutingActivation = false;
	bool bEndedDuringActivation = false;
	bool bEndedDuringActivationCancelled = false;
};
