// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "BaseDeathGameplayAbility.generated.h"

class UBaseHealthComponent;
class ACharacter;
class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;

UCLASS()
class GASCORE_API UBaseDeathGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UBaseDeathGameplayAbility();

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
	void OnDeathMontageCompleted();

	UFUNCTION()
	void OnDeathMontageBlendOut();

	UFUNCTION()
	void OnDeathMontageInterrupted();

	UFUNCTION()
	void OnDeathMontageCancelled();

	UFUNCTION()
	void OnDeathCompletionTimeout();

	UFUNCTION(BlueprintPure, Category = "Death")
	UBaseHealthComponent* GetHealthComponentFromAvatar() const;

	UFUNCTION(BlueprintCallable, Category = "Death")
	void FinishDeath();

	UFUNCTION(BlueprintCallable, Category = "Death")
	void FinishDeathWithCancel(bool bWasCancelled);

	UFUNCTION(BlueprintCallable, Category = "Death")
	bool PlayDeathMontage();

	UFUNCTION(BlueprintCallable, Category = "Death")
	void ApplyDeathState();

	UFUNCTION(BlueprintImplementableEvent, Category = "Death", meta = (DisplayName = "On Death Started"))
	void K2_OnDeathStarted(const FGameplayEventData& TriggerEventData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Death", meta = (DisplayName = "On Death Finished"))
	void K2_OnDeathFinished(bool bWasCancelled);

	UPROPERTY(BlueprintReadOnly, Category = "Death")
	TObjectPtr<UBaseHealthComponent> CachedHealthComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Montage")
	TObjectPtr<UAnimMontage> DeathMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Montage", meta = (ClampMin = "0.0"))
	float DeathMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Montage")
	FName DeathMontageStartSection = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Montage")
	bool bStopDeathMontageWhenAbilityEnds = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bFinishDeathWhenMontageEnds = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bAutoFinishDeathWithoutMontage = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bDisableMovement = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bDisableCapsuleCollision = false;

	/** Added to the expected montage duration before the safety fallback fires. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death", meta = (ClampMin = "0.0", Units = "s"))
	float DeathCompletionGracePeriod = 1.0f;

	/** Used when the montage duration cannot be determined. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death", meta = (ClampMin = "0.1", Units = "s"))
	float DeathCompletionFallbackTimeout = 10.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Death|Montage")
	TObjectPtr<UAbilityTask_PlayMontageAndWait> DeathMontageTask;

	UPROPERTY(BlueprintReadOnly, Category = "Death")
	bool bDeathFinished = false;

	FTimerHandle DeathCompletionTimerHandle;
};
