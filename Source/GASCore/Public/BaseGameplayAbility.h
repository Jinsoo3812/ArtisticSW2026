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

	// GA의 시작 지점 함수
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// GA의 종료 지점 함수
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/*
	* 대상에게 GE를 적용하는 함수
	* @param TargetData - GE를 적용할 대상 정보
	* @param EffectClass - 적용할 GE 클래스
	* @param EffectLevel - GE의 레벨 (옵션, 기본값은 1)
	* @return 적용된 GE 핸들의 배열 (여러 대상에게 적용할 경우 활용)
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual TArray<FActiveGameplayEffectHandle> ApplyEffectToTargetData(
		const FGameplayAbilityTargetDataHandle& TargetData,
		TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);

	/*
	* 자신에게 GE를 적용할 때 사용하는 헬퍼 함수
	* @param EffectClass - 적용할 GE 클래스
	* @param EffectLevel - GE의 레벨 (옵션, 기본값은 1)
	* @return 적용된 GE 핸들
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual FActiveGameplayEffectHandle ApplyEffectToOwner(TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);
};
