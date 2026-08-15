#pragma once

#include "CoreMinimal.h"
#include "BossAI/BossDeckPointSelector.h"
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
	void Reappear();

	UFUNCTION()
	void HandleMontageInterrupted();

	bool ResolveDestination();
	void FinishVanish(bool bWasCancelled);
	void ClearHiddenState();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish")
	TObjectPtr<UAnimMontage> PreparationMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.0", Units = "s"))
	float PreparationDelay = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Point")
	FBossDestinationSelectionSettings PointSelectionSettings;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> PreparationTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> HiddenTask = nullptr;

	FActiveGameplayEffectHandle HiddenStateHandle;
};
