// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayAbility.generated.h"

UCLASS()
class GASCORE_API UBaseGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UBaseGameplayAbility();

	// GA???쒖옉 吏???⑥닔
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// GA??醫낅즺 吏???⑥닔
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/*
	* ??곸뿉寃?GE瑜??곸슜?섎뒗 ?⑥닔
	* @param TargetData - GE瑜??곸슜??????뺣낫
	* @param EffectClass - ?곸슜??GE ?대옒??
	* @param EffectLevel - GE???덈꺼 (?듭뀡, 湲곕낯媛믪? 1)
	* @return ?곸슜??GE ?몃뱾??諛곗뿴 (?щ윭 ??곸뿉寃??곸슜??寃쎌슦 ?쒖슜)
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual TArray<FActiveGameplayEffectHandle> ApplyEffectToTargetData(
		const FGameplayAbilityTargetDataHandle& TargetData,
		TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);

	/*
	* ?먯떊?먭쾶 GE瑜??곸슜?????ъ슜?섎뒗 ?ы띁 ?⑥닔
	* @param EffectClass - ?곸슜??GE ?대옒??
	* @param EffectLevel - GE???덈꺼 (?듭뀡, 湲곕낯媛믪? 1)
	* @return ?곸슜??GE ?몃뱾
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual FActiveGameplayEffectHandle ApplyEffectToOwner(TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);
};
