#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "GA_BossVanish.generated.h"

class AShipBossEnemy;
class AAIController;
class UAbilityTask_BossVanishRelocation;

/** One prepared vanish followed by a weapon attack. Cooldown begins when the attack montage ends. */
UCLASS()
class ENEMY_API UGA_BossVanish : public UGA_BossBasicAttack
{
	GENERATED_BODY()
public:
	UGA_BossVanish();
	virtual bool OwnsPreselectedDestinationAfterCommit() const override { return true; }
	float GetHiddenLeadTime() const { return HiddenDuration; }
	float GetRelocationSettleTime() const { return RelocationSettleTime; }
	float GetBossCooldownDuration() const { return CooldownDuration; }
	FGameplayTag GetBossCooldownTag() const { return CooldownTag; }
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	virtual bool PlayAttackMontage(const FEnemyBasicAttackExecutionData& AttackData) override;
	virtual bool IsHitScanAllowed() const override { return bAttackMontageStarted; }
	virtual void HandleRevealedAtDestination();
	bool StartPreparedRelocation();
	bool CanAttackFromCurrentPosition() const;
	virtual float GetAttackRangeInset() const { return 0.f; }
	AActor* GetVanishTarget() const { return VanishTarget.Get(); }
	bool IsVanishTargetValid() const;
	void StartAttackAtDestination();
	void SetVanishTags(FGameplayTag AbilityTag, FGameplayTag InCooldownTag);
	void ApplyVanishCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;
	AShipBossEnemy* GetBossAvatar() const;
	UFUNCTION() void OnRelocationRevealed();
	UFUNCTION() void OnRelocationFailed();
	UFUNCTION() void OnVanishDeparture(FVector Location);
	UFUNCTION() void OnVanishArrival(FVector Location);

	/** Used for both the initial wait of ChainVanish and the cooldown after its attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Cooldown", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownDuration = 7.f;
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Boss|Cooldown")
	FGameplayTag CooldownTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish")
	TObjectPtr<UAnimMontage> PreparationMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.0", Units = "s"))
	float PreparationDelay = 0.45f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenDuration = 0.35f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float RelocationSettleTime = 0.1f;
	/** One-shot world-space feedback; configure GCN assets for these tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag DepartureGameplayCueTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag ArrivalGameplayCueTag;

private:
	UPROPERTY() TObjectPtr<UAbilityTask_BossVanishRelocation> RelocationTask;
	UPROPERTY() FGameplayTagContainer VanishCooldownTags;
	bool bAttackMontageStarted = false;
	bool bConsumedVanish = false;
	TWeakObjectPtr<AActor> VanishTarget;
	TWeakObjectPtr<AAIController> FocusController;
	FTimerHandle TargetValidationTimer;
	void ValidateLockedTarget();
};

/** Front-placement variant. Its initial destination relation remains authored by the BT selector. */
UCLASS()
class ENEMY_API UGA_BossVanishV2 : public UGA_BossVanish
{
	GENERATED_BODY()
public:
	UGA_BossVanishV2();
};
