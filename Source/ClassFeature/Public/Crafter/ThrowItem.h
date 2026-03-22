// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "ThrowItem.generated.h"

class ABaseProjectile;

/**
 * 
 */
UCLASS(Config = Game)
class CLASSFEATURE_API UThrowItem : public UBaseGameplayAbility
{
	GENERATED_BODY()
	
	/* --- GA 가상함수 --- */
public:
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

	/* --- Item 투척 관련 --- */
protected:
	// 투척 힘 (초기 속도)
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float ThrowSpeed = 1500.f;

	// 직선으로 던지지 않도록 살짝 들어올림
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float Upper = 0.15f;

	// 발사 궤적 업데이트 빈도
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float TrajectoryFrequency = 0.03f;

	// 발사체 Actor 네트워크 업데이트 빈도
	UPROPERTY(Config, EditDefaultsOnly, Category = "Throw")
	float ProjectileNetUpdateFrequency = 20.f;

	// 궤적 타이머 핸들
	FTimerHandle TrajectoryTimerHandle;

	// 궤적을 그리는 함수
	UFUNCTION()
	void DrawTrajectory();

	// 마우스 좌클릭 수신 시 호출되는 함수
	UFUNCTION()
	void OnConfirmEventReceived(FGameplayEventData Payload);

	// 스킬 키를 뗐을 때 취소 처리할 함수
	UFUNCTION()
	void OnInputReleased(float TimeHeld);
};
