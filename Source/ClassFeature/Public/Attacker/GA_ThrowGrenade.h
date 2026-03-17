// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_ThrowGrenade.generated.h"

class AGrenadeProjectile;
class UGameplayEffect;

/**
 * 수류탄 투척용 Gameplay Ability
 */
UCLASS()
class CLASSFEATURE_API UGA_ThrowGrenade : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
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

	// 스킬 키 입력이 해제되었을 때 호출될 함수 (투척 확정)
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	/* --- 수류탄 투척 관련 --- */
protected:
	// 수류탄 데미지 이펙트 (GE)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grenade")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// 투척 힘 (초기 속도)
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float ThrowSpeed = 1500.f;

	// 직선으로 던지지 않도록 살짝 들어올림
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float Upper = 0.15f;

	// 발사 궤적 업데이트 빈도
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float TrajectoryFrequency = 0.03f;

	// 궤적 타이머 핸들
	FTimerHandle TrajectoryTimerHandle;

	// 궤적을 그리는 함수
	UFUNCTION()
	void DrawTrajectory();

	// 마우스 우클릭 이벤트 수신 시 호출되는 함수 (조준 취소)
	UFUNCTION()
	void OnCancelEventReceived(FGameplayEventData Payload);
};