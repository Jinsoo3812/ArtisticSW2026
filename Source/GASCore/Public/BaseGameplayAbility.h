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

	// Ability가 활성화될 때 호출되는 진입점입니다. 공통 시작 로직을 넣는 위치입니다.
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// Ability가 종료될 때 호출되는 지점입니다. 공통 정리 로직을 넣는 위치입니다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/*
	* TargetData에 포함된 모든 대상에게 GameplayEffect를 적용합니다.
	* @param TargetData GE를 적용할 대상 정보입니다.
	* @param EffectClass 적용할 GameplayEffect 클래스입니다.
	* @param EffectLevel GE 레벨입니다. 기본값은 1입니다.
	* @return 적용된 GE 핸들 목록입니다.
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual TArray<FActiveGameplayEffectHandle> ApplyEffectToTargetData(
		const FGameplayAbilityTargetDataHandle& TargetData,
		TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);

	/*
	* Ability 소유자 자신의 ASC에 GameplayEffect를 적용합니다.
	* @param EffectClass 적용할 GameplayEffect 클래스입니다.
	* @param EffectLevel GE 레벨입니다. 기본값은 1입니다.
	* @return 적용된 GE 핸들입니다.
	*/
	UFUNCTION(BlueprintCallable, Category = "Ability|Effect")
	virtual FActiveGameplayEffectHandle ApplyEffectToOwner(TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel = 1);
};
