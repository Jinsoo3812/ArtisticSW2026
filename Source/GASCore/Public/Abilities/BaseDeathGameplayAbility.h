// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "BaseDeathGameplayAbility.generated.h"

class UBaseHealthComponent;

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
	UFUNCTION(BlueprintPure, Category = "Death")
	UBaseHealthComponent* GetHealthComponentFromAvatar() const;

	UFUNCTION(BlueprintCallable, Category = "Death")
	void FinishDeath();

	UFUNCTION(BlueprintImplementableEvent, Category = "Death", meta = (DisplayName = "On Death Started"))
	void K2_OnDeathStarted(const FGameplayEventData& TriggerEventData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Death", meta = (DisplayName = "On Death Finished"))
	void K2_OnDeathFinished(bool bWasCancelled);

	UPROPERTY(BlueprintReadOnly, Category = "Death")
	TObjectPtr<UBaseHealthComponent> CachedHealthComponent;
};
