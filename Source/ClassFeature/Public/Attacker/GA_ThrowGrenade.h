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
	virtual void DrawTrajectory();

	// 입력 해제(좌클릭 뗌) 시 호출될 함수
	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	// 취소 이벤트(우클릭) 수신 시 호출될 함수
	UFUNCTION()
	void OnRightClickCancelled(FGameplayEventData Payload);



	// 투척 애니메이션 몽타주
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* ThrowMontage;

	// 애니메이션 노티파이에서 기다릴 이벤트 태그
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	FGameplayTag ThrowEventTag;

	// 노티파이(이벤트)를 받았을 때 실행될 함수 (실제 수류탄 스폰)
	UFUNCTION()
	virtual void OnThrowEventReceived(FGameplayEventData Payload);

	// 몽타주 재생이 완료되거나 끊겼을 때 실행될 함수
	UFUNCTION()
	void OnMontageCompleted();

	// 클릭을 뗐을 때 점프할 몽타주 섹션의 이름
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	FName ThrowSectionName = FName("Throw");
};