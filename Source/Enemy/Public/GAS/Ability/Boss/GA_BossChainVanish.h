#pragma once

#include "CoreMinimal.h"
#include "BossAI/BossDeckPointSelector.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "GA_BossChainVanish.generated.h"

/** Up to three relocations, stopping at the first position that allows one melee attack. */
UCLASS()
class ENEMY_API UGA_BossChainVanish : public UGA_BossVanish
{
	GENERATED_BODY()
public:
	UGA_BossChainVanish();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
protected:
	virtual void HandleRevealedAtDestination() override;
	virtual float GetAttackRangeInset() const override { return AttackRangeInset; }
	/** Subtracted from weapon range only for ChainVanish's decision to attack (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Chain Vanish", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackRangeInset = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Chain Vanish", meta = (ClampMin = "1", ClampMax = "3", UIMin = "1", UIMax = "3"))
	int32 MaximumVanishCount = 3;
	/** Used only for retries; the first destination is still prepared by the BT. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Chain Vanish")
	FBossDestinationSelectionSettings RetrySelectionSettings;
private:
	void HandleCombatStateChanged(FGameplayTag Tag, int32 NewCount);
	void UnbindCombatState();
	TWeakObjectPtr<UAbilitySystemComponent> CombatASC;
	FDelegateHandle CombatStateDelegate;
	FGameplayAbilitySpecHandle GrantedHandle;
	bool bInitialCooldownApplied = false;
	int32 CompletedVanishCount = 0;
};
