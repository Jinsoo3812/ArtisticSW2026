#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GA_BossVanish.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAnimMontage;

UCLASS()
class ENEMY_API UGA_BossVanish : public UBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BossVanish();
	float GetHiddenLeadTime() const { return HiddenDuration; }
	float GetRelocationSettleTime() const { return RelocationSettleTime; }

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	UFUNCTION()
	void BeginHiddenPhase();

	UFUNCTION()
	void RelocateHidden();

	UFUNCTION()
	void RevealAtDestination();

	UFUNCTION()
	void HandleMontageInterrupted();

	bool ValidatePreselectedDestination() const;
	void FinishVanish(bool bWasCancelled);
	void ClearHiddenState();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish")
	TObjectPtr<UAnimMontage> PreparationMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.0", Units = "s"))
	float PreparationDelay = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenDuration = 0.35f;

	/** Keeps the Boss hidden while the teleported transform reaches remote clients. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float RelocationSettleTime = 0.1f;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> PreparationTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> HiddenTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> RelocationSettleTask = nullptr;

	FActiveGameplayEffectHandle HiddenStateHandle;
};

/** Front-placement Vanish variant. Destination relation remains authored by the BT selector. */
UCLASS()
class ENEMY_API UGA_BossVanishV2 : public UGA_BossVanish
{
	GENERATED_BODY()

public:
	UGA_BossVanishV2();
};
