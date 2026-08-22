#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Task/BTT_ActivateEnemyAbilityByTag.h"

#include "BTT_ActivateBossAbility.generated.h"

/**
 * Backward-compatible Boss specialization of the shared Enemy ability task.
 * Existing Boss BT assets keep their class while only Boss destination and
 * weapon-source selection remain specialized here.
 */
UCLASS()
class ENEMY_API UBTT_ActivateBossAbility : public UBTT_ActivateEnemyAbilityByTag
{
	GENERATED_BODY()

public:
	UBTT_ActivateBossAbility();

	bool RequiresPreselectedDestination() const { return bRequirePreselectedDestination; }
	bool PrefersCurrentWeaponAbility() const { return bPreferCurrentWeaponAbility; }

	virtual FString GetStaticDescription() const override;

protected:
	virtual const FGameplayAbilitySpec* FindAbilitySpec(
		APawn& Pawn,
		const UAbilitySystemComponent& AbilitySystem) const override;
	virtual bool ValidateActivationContext(
		APawn& Pawn,
		const UAbilitySystemComponent& AbilitySystem) const override;
	virtual void OnAbilityTaskFinished(EBTNodeResult::Type Result) override;

	/** Prefer an ability spec granted by the currently equipped weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ability")
	bool bPreferCurrentWeaponAbility = true;

	/** Mobility branches can require BTT_SelectBossDestinationPoint to run first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	bool bRequirePreselectedDestination = false;

	/** Keeps the server Blackboard and replicated Boss destination from becoming stale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	bool bClearDestinationWhenFinished = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Destination")
	FBlackboardKeySelector DestinationPointKey;

private:
	void ResetDestinationState();
};
