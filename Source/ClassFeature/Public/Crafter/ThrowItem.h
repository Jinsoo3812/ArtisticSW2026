// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "ThrowItem.generated.h"

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

	// 스킬 키 입력이 해제되었을 때 호출될 함수
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	/* --- Item 투척 관련 --- */
protected:
	// 투척 힘 (초기 속도)
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float ThrowSpeed = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float TrajectoryFrequency = 0.03f;

	// 서버에서 스폰할 "진짜" 복제 투사체 클래스 (bReplicates = true 필수)
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	TSubclassOf<AActor> ReplicatedProjectileClass;

	// 발사체 네트워크 업데이트 빈도
	UPROPERTY(Config, EditDefaultsOnly, Category = "Throw")
	float ProjectileNetUpdateFrequency = 20.f;

	// 궤적 그리기 루프를 위한 타이머 핸들
	FTimerHandle TrajectoryTimerHandle;

	// 발사가 확정되었는지 추적하는 상태 플래그
	bool bIsConfirmed = false;

	// 조준 시작
	void StartAiming();

	// 궤적 그리기
	void DrawTrajectory();

	/*
	* 마우스 포인터 따라가기
	* @param OutHitResult - 트레이스 결과를 저장할 FHitResult 참조
	* @param TraceDistance - 트레이스 최대 거리 (커서 방향으로 카메라에서 얼마나 떨어질지)
	*/
	bool TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance);

	// 마우스 좌클릭 이벤트 수신 시 호출되는 함수 (투척)
	UFUNCTION()
	void OnConfirmEventReceived(FGameplayEventData Payload);

	// 클라이언트에서 계산된 타겟 위치를 서버로 전송하는 함수
	UFUNCTION()
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
};
