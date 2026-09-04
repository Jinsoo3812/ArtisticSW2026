// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "PlayerCombatGameplayAbility.generated.h"

/**
 * Common base class for all player combat, offensive, weapon, and skill abilities.
 * Automatically enforces action restrictions: blocked during swimming, dodging (rolling),
 * hit reaction (damaged), and death without requiring redundant overrides in individual abilities.
 */
UCLASS(Abstract)
class CLASSFEATURE_API UPlayerCombatGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UPlayerCombatGameplayAbility();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};
