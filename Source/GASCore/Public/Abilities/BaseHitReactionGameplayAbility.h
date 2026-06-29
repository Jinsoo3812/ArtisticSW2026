// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "BaseHitReactionGameplayAbility.generated.h"

UCLASS()
class GASCORE_API UBaseHitReactionGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UBaseHitReactionGameplayAbility();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "HitReaction", meta = (DisplayName = "On Hit Reaction Started"))
	void K2_OnHitReactionStarted(const FGameplayEventData& TriggerEventData, float DamageAmount);
};
