// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "Interact.generated.h"

/**
 * 
 */
UCLASS()
class CLASSFEATURE_API UInteract : public UBaseGameplayAbility
{
	GENERATED_BODY()
	
protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 로컬에서 Trace 수행
	void PerformLocalTrace(FHitResult& OutHitResult);

	// [서버]에서 클라이언트의 TargetData를 수신했을 때 호출되는 콜백
	UFUNCTION()
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	// [서버] 수신 및 검증이 완료된 후 실제 Interact 수행 함수
	void ProcessInteract(const FGameplayAbilityTargetDataHandle& InData);

	// 상호작용 광선(Sweep) 길이
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	float InteractTraceDistance = 100.0f;

	// 상호작용 탐색 구체(Sphere)의 반경
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	float InteractTraceRadius = 50.0f;
};
